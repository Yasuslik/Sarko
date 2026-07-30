#include "Net/SarkoBackendClient.h"

#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Serialises stacks as the backend's `[{"item_id","quantity"}]`, dropping non-positive quantities. */
	FString StacksToJsonArray(const TArray<FSarkoItemStack>& Stacks)
	{
		TArray<FString> Parts;
		Parts.Reserve(Stacks.Num());
		for (const FSarkoItemStack& Stack : Stacks)
		{
			if (Stack.Quantity <= 0)
			{
				// domain.ValidateStacks rejects the whole request for one
				// non-positive quantity, and a stray zero must not cost the
				// player the rest of the haul.
				continue;
			}
			Parts.Add(FString::Printf(TEXT("{\"item_id\":\"%s\",\"quantity\":%d}"),
				*Stack.Item.ToString(), Stack.Quantity));
		}
		return FString::Printf(TEXT("[%s]"), *FString::Join(Parts, TEXT(",")));
	}

	/** Parses a root JSON object, or fails with a named error. */
	bool ReadRoot(const FString& Json, TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			OutError = TEXT("response was not valid JSON");
			return false;
		}
		return true;
	}
}

FString SarkoBackend::MakeAnonymousBody(const FString& DeviceId)
{
	return FString::Printf(TEXT("{\"device_id\":\"%s\"}"), *DeviceId);
}

FString SarkoBackend::MakeRaidStartBody(const FString& MapId, const TArray<FSarkoItemStack>& Loadout)
{
	return FString::Printf(TEXT("{\"map_id\":\"%s\",\"loadout\":%s}"), *MapId, *StacksToJsonArray(Loadout));
}

FString SarkoBackend::MakeSessionBody(const FString& SessionId, const FString& SessionToken)
{
	return FString::Printf(TEXT("{\"session_id\":\"%s\",\"session_token\":\"%s\"}"), *SessionId, *SessionToken);
}

FString SarkoBackend::MakeRaidResultBody(const FString& SessionId, const FString& SessionToken,
	const FString& Outcome, const TArray<FSarkoItemStack>& Items)
{
	return FString::Printf(
		TEXT("{\"session_id\":\"%s\",\"session_token\":\"%s\",\"outcome\":\"%s\",\"items\":%s}"),
		*SessionId, *SessionToken, *Outcome, *StacksToJsonArray(Items));
}

bool SarkoBackend::ParseAnonymousResponse(const FString& Json, FString& OutPlayerId, FString& OutToken, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}
	if (!Root->TryGetStringField(TEXT("player_id"), OutPlayerId) || OutPlayerId.IsEmpty())
	{
		OutError = TEXT("auth response has no 'player_id'");
		return false;
	}
	if (!Root->TryGetStringField(TEXT("token"), OutToken) || OutToken.IsEmpty())
	{
		OutError = TEXT("auth response has no 'token'");
		return false;
	}
	return true;
}

bool SarkoBackend::ParseRaidStartResponse(const FString& Json, FSarkoRaidSession& OutSession, FString& OutError)
{
	// Reset first: a half-filled session is worse than none, because a caller
	// that ignores the return value would submit a result with a blank token and
	// get a 401 it cannot explain.
	OutSession = FSarkoRaidSession();

	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}
	if (!Root->TryGetStringField(TEXT("session_id"), OutSession.SessionId) || OutSession.SessionId.IsEmpty())
	{
		OutError = TEXT("raid/start response has no 'session_id'");
		OutSession = FSarkoRaidSession();
		return false;
	}
	if (!Root->TryGetStringField(TEXT("session_token"), OutSession.SessionToken) || OutSession.SessionToken.IsEmpty())
	{
		OutError = TEXT("raid/start response has no 'session_token'");
		OutSession = FSarkoRaidSession();
		return false;
	}

	// Read as a double, because the value can exceed INT32_MAX and the JSON
	// reader has no int64 accessor. Every uint32 is exactly representable in a
	// double, so nothing is lost before SeedToInt32 wraps it.
	double Seed = 0.0;
	if (!Root->TryGetNumberField(TEXT("seed"), Seed))
	{
		OutError = TEXT("raid/start response has no 'seed'");
		OutSession = FSarkoRaidSession();
		return false;
	}
	OutSession.Seed = SeedToInt32(static_cast<int64>(Seed));

	FString ExpiresAt;
	if (!Root->TryGetStringField(TEXT("expires_at"), ExpiresAt) ||
		!FDateTime::ParseIso8601(*ExpiresAt, OutSession.ExpiresAt))
	{
		OutError = TEXT("raid/start response has no parseable 'expires_at'");
		OutSession = FSarkoRaidSession();
		return false;
	}
	return true;
}

bool SarkoBackend::ParseExpiresAtResponse(const FString& Json, FDateTime& OutExpiresAt, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}
	FString ExpiresAt;
	if (!Root->TryGetStringField(TEXT("expires_at"), ExpiresAt) ||
		!FDateTime::ParseIso8601(*ExpiresAt, OutExpiresAt))
	{
		OutError = TEXT("response has no parseable 'expires_at'");
		return false;
	}
	return true;
}

bool SarkoBackend::ParseErrorResponse(const FString& Json, FSarkoBackendError& OutError)
{
	OutError = FSarkoBackendError();

	TSharedPtr<FJsonObject> Root;
	FString Ignored;
	if (!ReadRoot(Json, Root, Ignored))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Envelope = nullptr;
	if (!Root->TryGetObjectField(TEXT("error"), Envelope) || !Envelope)
	{
		return false;
	}
	(*Envelope)->TryGetStringField(TEXT("code"), OutError.Code);
	(*Envelope)->TryGetStringField(TEXT("message"), OutError.Message);
	return !OutError.Code.IsEmpty();
}

int32 SarkoBackend::SeedToInt32(int64 Seed)
{
	// Truncate to 32 bits, then reinterpret. Well-defined both ways, and the
	// same bits FRandomStream would have got from the backend's uint32.
	return static_cast<int32>(static_cast<uint32>(static_cast<uint64>(Seed) & 0xFFFFFFFFull));
}

FString SarkoBackend::DeviceIdFilePath()
{
	// Under Saved/ so it survives a rebuild, is never committed, and is a plain
	// runtime file rather than an asset — this project ships no binary assets.
	return FPaths::ProjectSavedDir() / TEXT("SarkoDevice.txt");
}

FString SarkoBackend::EnsureDeviceId()
{
	const FString Path = DeviceIdFilePath();

	FString Existing;
	if (FFileHelper::LoadFileToString(Existing, *Path))
	{
		Existing.TrimStartAndEndInline();
		if (!Existing.IsEmpty() && Existing.Len() <= 128)
		{
			return Existing;
		}
	}

	// A GUID, not a hardware id: 36 characters, inside the backend's 128-char
	// cap, and it identifies an install rather than a machine.
	const FString Fresh = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	if (!FFileHelper::SaveStringToFile(Fresh, *Path))
	{
		// Not fatal, but loud: without persistence every launch is a new player
		// with a new stash, which looks exactly like the backend losing data.
		UE_LOG(LogTemp, Error, TEXT("SarkoBackend: could not persist the device id to %s — progress will not carry across launches"),
			*Path);
	}
	return Fresh;
}

float SarkoBackend::ClockSecondsFromDeadline(float MapDurationSeconds, double SecondsUntilDeadline, float GraceMarginSeconds)
{
	/** Never end a raid on the spawn frame, whatever the server said. */
	constexpr float MinimumPlayableSeconds = 30.f;

	const float FromMap = MapDurationSeconds > 0.f
		? MapDurationSeconds
		: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;

	const float FromServer = static_cast<float>(SecondsUntilDeadline) - FMath::Max(0.f, GraceMarginSeconds);

	// The map is the ceiling and the server is the other ceiling; the floor stops
	// a bad clock or an old deadline from producing a raid that is already over.
	return FMath::Max(MinimumPlayableSeconds, FMath::Min(FromMap, FMath::Max(FromServer, MinimumPlayableSeconds)));
}

const TCHAR* SarkoBackend::OutcomeToWire(ESarkoRaidOutcome Outcome)
{
	// Only Extracted maps to "extracted". Everything else — including a state
	// that should never reach here — is "died", because the failure direction
	// that costs a player their haul is recoverable and the one that grants free
	// loot is not.
	return Outcome == ESarkoRaidOutcome::Extracted ? TEXT("extracted") : TEXT("died");
}

TArray<FSarkoItemStack> SarkoBackend::StarterLoadout()
{
	// Mirrors domain.StarterKit() minus the medkit. Keep them in step: this is
	// debited from the stash at /v1/raid/start, and asking for more than the kit
	// granted is 409 insufficient_items on a brand-new player's first raid.
	TArray<FSarkoItemStack> Loadout;
	Loadout.Add(FSarkoItemStack{ TEXT("pistol"), 1 });
	Loadout.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 60 });
	return Loadout;
}

void FSarkoBackendClient::Send(const FString& Path, const FString& Body, bool bAuthenticated,
	TFunction<void(bool, const FString&, const FString&)> OnComplete)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	if (!Settings.bBackendEnabled)
	{
		OnComplete(false, FString(), TEXT("the backend is disabled in settings"));
		return;
	}
	if (bAuthenticated && Jwt.IsEmpty())
	{
		OnComplete(false, FString(), TEXT("no token: authenticate first"));
		return;
	}

	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings.BackendBaseUrl + Path);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (bAuthenticated)
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Jwt);
	}
	Request->SetContentAsString(Body);
	// Bounded, because a stalled request must not hold the end of a raid open.
	Request->SetTimeout(Settings.BackendTimeoutSeconds);

	// Weak self: an HTTP completion routinely fires after the world — and this
	// client with it — has been torn down.
	TWeakPtr<FSarkoBackendClient> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindLambda(
		[WeakSelf, Path, OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!WeakSelf.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("SarkoBackend: %s completed after the client was destroyed; ignored"), *Path);
				return;
			}
			if (!bConnected || !Response.IsValid())
			{
				const FString Error = FString::Printf(TEXT("%s: no response (offline?)"), *Path);
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *Error);
				OnComplete(false, FString(), Error);
				return;
			}

			const int32 Code = Response->GetResponseCode();
			const FString ResponseBody = Response->GetContentAsString();
			if (Code < 200 || Code >= 300)
			{
				FSarkoBackendError Parsed;
				const FString Error = SarkoBackend::ParseErrorResponse(ResponseBody, Parsed)
					? FString::Printf(TEXT("%s: HTTP %d %s — %s"), *Path, Code, *Parsed.Code, *Parsed.Message)
					: FString::Printf(TEXT("%s: HTTP %d"), *Path, Code);
				// Error, not Warning: every one of these costs the player a
				// raid's worth of persistence, and the log is the only place it
				// is visible.
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *Error);
				OnComplete(false, ResponseBody, Error);
				return;
			}

			OnComplete(true, ResponseBody, FString());
		});

	if (!Request->ProcessRequest())
	{
		const FString Error = FString::Printf(TEXT("%s: the request could not be dispatched"), *Path);
		UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *Error);
		OnComplete(false, FString(), Error);
	}
}

void FSarkoBackendClient::Authenticate(FOnDone OnDone)
{
	TWeakPtr<FSarkoBackendClient> WeakSelf = AsShared();
	Send(TEXT("/v1/auth/anonymous"), SarkoBackend::MakeAnonymousBody(SarkoBackend::EnsureDeviceId()),
		/*bAuthenticated*/ false,
		[WeakSelf, OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			TSharedPtr<FSarkoBackendClient> Self = WeakSelf.Pin();
			if (!Self)
			{
				return;
			}
			if (!bSuccess)
			{
				OnDone(false, Error);
				return;
			}
			FString ParseError;
			if (!SarkoBackend::ParseAnonymousResponse(Body, Self->PlayerId, Self->Jwt, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, ParseError);
				return;
			}
			// The token is never logged. The player id is, because it is the only
			// way to find this player's rows in the database from a log line.
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: authenticated as player %s"), *Self->PlayerId);
			OnDone(true, FString());
		});
}

void FSarkoBackendClient::StartRaid(const FString& MapId, const TArray<FSarkoItemStack>& Loadout, FOnSession OnDone)
{
	Send(TEXT("/v1/raid/start"), SarkoBackend::MakeRaidStartBody(MapId, Loadout), /*bAuthenticated*/ true,
		[OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, FSarkoRaidSession(), Error);
				return;
			}
			FSarkoRaidSession Session;
			FString ParseError;
			if (!SarkoBackend::ParseRaidStartResponse(Body, Session, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, FSarkoRaidSession(), ParseError);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: raid session %s opened, seed %d"),
				*Session.SessionId, Session.Seed);
			OnDone(true, Session, FString());
		});
}

void FSarkoBackendClient::ConfirmRaid(const FSarkoRaidSession& Session, FOnDeadline OnDone)
{
	Send(TEXT("/v1/raid/confirm"), SarkoBackend::MakeSessionBody(Session.SessionId, Session.SessionToken),
		/*bAuthenticated*/ true,
		[OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, FDateTime(0), Error);
				return;
			}
			FDateTime ExpiresAt(0);
			FString ParseError;
			if (!SarkoBackend::ParseExpiresAtResponse(Body, ExpiresAt, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, FDateTime(0), ParseError);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: raid confirmed, server deadline %s"),
				*ExpiresAt.ToIso8601());
			OnDone(true, ExpiresAt, FString());
		});
}

void FSarkoBackendClient::SubmitResult(const FSarkoRaidSession& Session, const FString& Outcome,
	const TArray<FSarkoItemStack>& Items, FOnDone OnDone)
{
	Send(TEXT("/v1/raid/result"),
		SarkoBackend::MakeRaidResultBody(Session.SessionId, Session.SessionToken, Outcome, Items),
		/*bAuthenticated*/ true,
		[Outcome, OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, Error);
				return;
			}
			// The response carries credited_items and already_closed; logged
			// rather than parsed into a struct, because nothing in the raid acts
			// on them — the shelter reads the profile next launch.
			UE_LOG(LogTemp, Display, TEXT("SarkoBackend: result '%s' recorded: %s"), *Outcome, *Body);
			OnDone(true, FString());
		});
}
