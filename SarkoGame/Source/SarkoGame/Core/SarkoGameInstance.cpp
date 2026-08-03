#include "Core/SarkoGameInstance.h"

#include "Core/SarkoRaidSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/SarkoBackendClient.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

float SarkoResult::RetryDelaySeconds(int32 Attempt, float BaseSeconds, float MaxSeconds)
{
	const float Base = FMath::Max(0.f, BaseSeconds);
	const float Max = FMath::Max(Base, MaxSeconds);
	// Bounded exponent: 1 << 40 is undefined behaviour, and the cap makes every
	// attempt past the sixth identical anyway.
	const int32 Steps = FMath::Clamp(Attempt, 0, 16);
	return FMath::Min(Max, Base * static_cast<float>(1 << Steps));
}

FString SarkoResult::SerialisePending(const FSarkoPendingResult& Pending)
{
	// Hand-built, like SarkoBackend's request bodies: these four fields are a
	// session id, a token, one of three fixed words and a list of catalog ids —
	// none of which can contain a quote, because every one of them is either a
	// server-issued identifier or an FName out of items.json.
	FString Items;
	for (int32 Index = 0; Index < Pending.Items.Num(); ++Index)
	{
		Items += FString::Printf(TEXT("%s{\"item_id\":\"%s\",\"quantity\":%d}"),
			Index == 0 ? TEXT("") : TEXT(","),
			*Pending.Items[Index].Item.ToString(), Pending.Items[Index].Quantity);
	}
	return FString::Printf(
		TEXT("{\"session_id\":\"%s\",\"session_token\":\"%s\",\"outcome\":\"%s\",\"items\":[%s]}"),
		*Pending.SessionId, *Pending.SessionToken, *Pending.Outcome, *Items);
}

bool SarkoResult::ParsePending(const FString& Json, FSarkoPendingResult& Out, FString& OutError)
{
	Out = FSarkoPendingResult();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}
	Root->TryGetStringField(TEXT("session_id"), Out.SessionId);
	Root->TryGetStringField(TEXT("session_token"), Out.SessionToken);
	Root->TryGetStringField(TEXT("outcome"), Out.Outcome);

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Root->TryGetArrayField(TEXT("items"), Items) && Items)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Items)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				continue;
			}
			FString Id;
			double Quantity = 0.0;
			if ((*Object)->TryGetStringField(TEXT("item_id"), Id) && !Id.IsEmpty()
				&& (*Object)->TryGetNumberField(TEXT("quantity"), Quantity) && Quantity > 0.0)
			{
				Out.Items.Add(FSarkoItemStack{ FName(*Id), static_cast<int32>(Quantity) });
			}
		}
	}

	// A half-written file (a kill mid-save) is not a result: it is refused here
	// rather than submitted as a haul with no token, which the server would
	// answer 401 to forever.
	if (!Out.IsValid())
	{
		OutError = TEXT("a pending result needs a session id, a session token and an outcome");
		return false;
	}
	return true;
}

FString SarkoResult::PendingResultPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("SarkoPendingResult.json"));
}

void USarkoGameInstance::Init()
{
	Super::Init();

	// A result left behind by a previous launch — a crash, an iOS suspension, or
	// simply a network that was down when the raid ended. The server is
	// idempotent and dedups by session token, so resubmitting one it already took
	// is safe; losing one it never took is not.
	const FString Path = SarkoResult::PendingResultPath();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		return;
	}
	FString Error;
	if (!SarkoResult::ParsePending(Json, Pending, Error))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoGameInstance: %s is unreadable (%s) — removing it; the haul it described is lost"),
			*Path, *Error);
		FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Path);
		Pending = FSarkoPendingResult();
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("SarkoGameInstance: a raid result from a previous launch is unsent ('%s', %d stacks) — resubmitting"),
		*Pending.Outcome, Pending.Items.Num());
	PendingAttempt = 0;
	AttemptPendingSubmission();
}

void USarkoGameInstance::Shutdown()
{
	// The timer manager goes with this object; clearing the handle first keeps a
	// half-torn-down instance from being called back into.
	GetTimerManager().ClearTimer(PendingRetryTimer);
	Super::Shutdown();
}

TSharedPtr<FSarkoBackendClient> USarkoGameInstance::EnsureBackend()
{
	if (!Backend.IsValid())
	{
		Backend = MakeShared<FSarkoBackendClient>();
	}
	return Backend;
}

void USarkoGameInstance::PersistPending()
{
	const FString Path = SarkoResult::PendingResultPath();
	if (!Pending.IsValid())
	{
		FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Path);
		return;
	}
	if (!FFileHelper::SaveStringToFile(SarkoResult::SerialisePending(Pending), *Path))
	{
		// Loud, because the retry loop still runs this launch — what is lost is
		// only the ability to survive the app dying.
		UE_LOG(LogTemp, Error,
			TEXT("SarkoGameInstance: could not write %s — this result will not survive the app closing"), *Path);
	}
}

void USarkoGameInstance::SubmitRaidResultWithRetry(const FSarkoRaidSession& Session,
	const FString& WireOutcome, const TArray<FSarkoItemStack>& Items)
{
	if (Session.SessionId.IsEmpty() || Session.SessionToken.IsEmpty())
	{
		// An offline raid, or one whose session never opened. FinishRaid already
		// said so at Error; there is nothing here worth keeping.
		return;
	}

	// Written to disk BEFORE the first attempt, deliberately: the whole point is
	// to survive the process dying during the request.
	Pending.SessionId = Session.SessionId;
	Pending.SessionToken = Session.SessionToken;
	Pending.Outcome = WireOutcome;
	Pending.Items = Items;
	PendingAttempt = 0;
	PersistPending();

	GetTimerManager().ClearTimer(PendingRetryTimer);
	AttemptPendingSubmission();
}

void USarkoGameInstance::AttemptPendingSubmission()
{
	if (!Pending.IsValid() || bPendingInFlight)
	{
		return;
	}
	if (!GetDefault<USarkoRaidSettings>()->bBackendEnabled)
	{
		// Nothing to submit to. The file stays, so turning the backend back on and
		// relaunching still credits the haul.
		return;
	}

	bPendingInFlight = true;
	const int32 Attempt = PendingAttempt++;

	// TWeakObjectPtr, not `this`: a completion can land during shutdown. The
	// backend client is owned here rather than by a dying world precisely so the
	// request outlives the level — not so it can write into a dead instance.
	TWeakObjectPtr<USarkoGameInstance> WeakThis(this);

	const auto Finish = [WeakThis, Attempt](bool bSuccess, const FString& Error)
	{
		USarkoGameInstance* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bPendingInFlight = false;

		if (bSuccess)
		{
			UE_LOG(LogTemp, Display,
				TEXT("SarkoGameInstance: the pending raid result was accepted on attempt %d"), Attempt + 1);
			Self->Pending = FSarkoPendingResult();
			Self->PersistPending();
			// The shelter's "НЕ ЗБЕРЕЖЕНО" label, corrected — possibly a whole
			// launch later, which is exactly the case this exists for.
			Self->LastRaid.bPersisted = true;
			return;
		}

		if (Attempt + 1 >= MaxPendingAttempts)
		{
			UE_LOG(LogTemp, Error,
				TEXT("SarkoGameInstance: the raid result is still unsent after %d attempts (%s). ")
				TEXT("It stays in %s and the next launch will try again."),
				Attempt + 1, *Error, *SarkoResult::PendingResultPath());
			return;
		}

		const float Delay = SarkoResult::RetryDelaySeconds(
			Attempt, PendingRetryBaseSeconds, PendingRetryMaxSeconds);
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoGameInstance: raid result attempt %d failed (%s) — retrying in %.0fs"),
			Attempt + 1, *Error, Delay);
		// The game instance's OWN timer manager, which survives the travel back to
		// the shelter. A world timer would die with the raid it was started in,
		// which is the five seconds after every raid ends.
		Self->GetTimerManager().SetTimer(Self->PendingRetryTimer,
			FTimerDelegate::CreateWeakLambda(Self, [Self]() { Self->AttemptPendingSubmission(); }),
			Delay, /*bLoop*/ false);
	};

	TSharedPtr<FSarkoBackendClient> Client = EnsureBackend();
	FSarkoRaidSession Session;
	Session.SessionId = Pending.SessionId;
	Session.SessionToken = Pending.SessionToken;
	const FString Outcome = Pending.Outcome;
	const TArray<FSarkoItemStack> Items = Pending.Items;

	// Captured strongly, as FinishRaid's own submission was and for the same
	// reason: the lambda must outlive whatever world it was started from.
	const auto Post = [Client, Session, Outcome, Items, Finish]()
	{
		Client->SubmitResult(Session, Outcome, Items, Finish);
	};

	if (Client->IsAuthenticated())
	{
		Post();
		return;
	}
	// A resubmission on a fresh launch has no JWT yet — the shelter has not been
	// entered. Authenticating here rather than waiting for it means the haul is
	// credited before the player has looked at anything.
	Client->Authenticate([Post, Finish](bool bAuthenticated, const FString& Error)
	{
		if (!bAuthenticated)
		{
			Finish(false, FString::Printf(TEXT("could not authenticate: %s"), *Error));
			return;
		}
		Post();
	});
}

void USarkoGameInstance::RecordRaidOutcome(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul,
	bool bPersisted)
{
	LastRaid.Outcome = Outcome;
	LastRaid.Haul = Haul;
	LastRaid.bPersisted = bPersisted;

	// The profile is now stale by definition — the raid just changed the stash —
	// so the cached copy is marked unloaded rather than left to be drawn as if it
	// were current. The shelter re-fetches on entry; this is what stops it drawing
	// yesterday's stash for the one frame before that lands.
	bProfileLoaded = false;

	UE_LOG(LogTemp, Display, TEXT("SarkoGameInstance: raid ended %s with %d stacks, persisted: %s"),
		*UEnum::GetValueAsString(Outcome), Haul.Num(), bPersisted ? TEXT("yes") : TEXT("NO"));
}

void USarkoGameInstance::RecordProfile(const FSarkoProfile& Profile)
{
	CachedProfile = Profile;
	bProfileLoaded = true;
}

FSarkoRaidSession USarkoGameInstance::TakePendingSortie()
{
	// CONSUMED, not read. This is the only guarantee that one granted kit becomes at
	// most one raid: the raid game mode adopts the session by calling this, and a
	// second world — a second travel, a re-entered raid, a scripted double press —
	// finds nothing and starts an ordinary raid instead. Leaving it in place would let
	// one free run's session be confirmed twice, and the second confirm would be
	// answered `session_not_open` in the middle of a raid the player was playing.
	const FSarkoRaidSession Taken = PendingSortie;
	PendingSortie = FSarkoRaidSession();
	return Taken;
}
