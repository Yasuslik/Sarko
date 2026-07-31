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
	/**
	 * Escapes a string so it can sit inside a JSON double-quoted value.
	 *
	 * The bodies below are assembled by hand (so the exact field names stay
	 * visible in this file and cannot drift with a struct rename), which means
	 * every interpolated value has to be escaped here instead. Without it a
	 * designer-edited BackendMapId containing a quote, or any string with a
	 * newline, produces malformed JSON and an unexplained 400 — the request would
	 * look correct in the log and be rejected anyway.
	 *
	 * Deliberately **not** FString::ReplaceCharWithEscapedChar: its table maps `'`
	 * to `\'`, which is not a legal JSON escape, so it would turn one awkward
	 * character into the very 400 this prevents.
	 */
	FString EscapeJson(const FString& Value)
	{
		FString Out;
		Out.Reserve(Value.Len());
		for (const TCHAR Char : Value)
		{
			switch (Char)
			{
			case TEXT('\"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\b'): Out += TEXT("\\b"); break;
			case TEXT('\f'): Out += TEXT("\\f"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Char < 0x20)
				{
					// Every other control character has to be escaped too; JSON
					// forbids them raw. Anything above passes through, because Go
					// reads the body as UTF-8 and so does FString's conversion.
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Char));
				}
				else
				{
					Out += Char;
				}
				break;
			}
		}
		return Out;
	}

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
				*EscapeJson(Stack.Item.ToString()), Stack.Quantity));
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
	return FString::Printf(TEXT("{\"device_id\":\"%s\"}"), *EscapeJson(DeviceId));
}

FString SarkoBackend::MakeRaidStartBody(const FString& MapId, const TArray<FSarkoItemStack>& Loadout)
{
	return FString::Printf(TEXT("{\"map_id\":\"%s\",\"loadout\":%s}"),
		*EscapeJson(MapId), *StacksToJsonArray(Loadout));
}

FString SarkoBackend::MakeSessionBody(const FString& SessionId, const FString& SessionToken)
{
	return FString::Printf(TEXT("{\"session_id\":\"%s\",\"session_token\":\"%s\"}"),
		*EscapeJson(SessionId), *EscapeJson(SessionToken));
}

FString SarkoBackend::MakeRaidResultBody(const FString& SessionId, const FString& SessionToken,
	const FString& Outcome, const TArray<FSarkoItemStack>& Items)
{
	return FString::Printf(
		TEXT("{\"session_id\":\"%s\",\"session_token\":\"%s\",\"outcome\":\"%s\",\"items\":%s}"),
		*EscapeJson(SessionId), *EscapeJson(SessionToken), *EscapeJson(Outcome), *StacksToJsonArray(Items));
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

bool SarkoBackend::ParseProfileResponse(const FString& Json, FSarkoProfile& OutProfile, FString& OutError)
{
	// Reset first, and again on every failure below: a caller that ignores the
	// return value must find an empty profile rather than a plausible half of one.
	OutProfile = FSarkoProfile();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	if (!ReadRoot(Json, Root, OutError))
	{
		return false;
	}

	// An error envelope is valid JSON with none of these fields, and it arrives
	// on a 401 the moment the JWT expires. Named explicitly so the log says
	// "unauthorized" instead of "profile response has no 'player_id'".
	FSarkoBackendError Envelope;
	if (ParseErrorResponse(Json, Envelope))
	{
		OutError = FString::Printf(TEXT("profile request failed: %s — %s"), *Envelope.Code, *Envelope.Message);
		return false;
	}

	if (!Root->TryGetStringField(TEXT("player_id"), OutProfile.PlayerId) || OutProfile.PlayerId.IsEmpty())
	{
		OutError = TEXT("profile response has no 'player_id'");
		OutProfile = FSarkoProfile();
		return false;
	}
	if (!Root->TryGetStringField(TEXT("vehicle_tier"), OutProfile.VehicleTier) || OutProfile.VehicleTier.IsEmpty())
	{
		OutError = TEXT("profile response has no 'vehicle_tier'");
		OutProfile = FSarkoProfile();
		return false;
	}

	// Optional, and absence is not an error: schema_version is informational and
	// a future response may drop it.
	double SchemaVersion = 0.0;
	if (Root->TryGetNumberField(TEXT("schema_version"), SchemaVersion))
	{
		OutProfile.SchemaVersion = static_cast<int32>(SchemaVersion);
	}

	// `stash` may be absent (a brand-new player before the starter kit lands) or
	// empty. Present-but-not-an-array is a fault, and must not read as "empty".
	if (Root->HasField(TEXT("stash")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Stash = nullptr;
		if (!Root->TryGetArrayField(TEXT("stash"), Stash) || !Stash)
		{
			OutError = TEXT("profile response has a 'stash' that is not an array");
			OutProfile = FSarkoProfile();
			return false;
		}
		OutProfile.Stash.Reserve(Stash->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Stash)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = TEXT("'stash' contains a non-object entry");
				OutProfile = FSarkoProfile();
				return false;
			}
			FString ItemId;
			double Quantity = 0.0;
			if (!(*Object)->TryGetStringField(TEXT("item_id"), ItemId) || ItemId.IsEmpty())
			{
				OutError = TEXT("a stash row has no 'item_id'");
				OutProfile = FSarkoProfile();
				return false;
			}
			if (!(*Object)->TryGetNumberField(TEXT("quantity"), Quantity) || Quantity < 1.0)
			{
				OutError = FString::Printf(TEXT("stash row '%s' has no positive 'quantity'"), *ItemId);
				OutProfile = FSarkoProfile();
				return false;
			}
			OutProfile.Stash.Add(FSarkoItemStack{ FName(*ItemId), static_cast<int32>(Quantity) });
		}
	}

	if (Root->HasField(TEXT("unlocked_maps")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
		if (!Root->TryGetArrayField(TEXT("unlocked_maps"), Maps) || !Maps)
		{
			OutError = TEXT("profile response has an 'unlocked_maps' that is not an array");
			OutProfile = FSarkoProfile();
			return false;
		}
		OutProfile.UnlockedMaps.Reserve(Maps->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Maps)
		{
			FString MapId;
			if (Value->TryGetString(MapId) && !MapId.IsEmpty())
			{
				OutProfile.UnlockedMaps.Add(MapId);
			}
		}
	}

	// The one field whose absence is meaningful rather than merely tolerated:
	// absent == false == tutorial mode (spec §6.5).
	Root->TryGetBoolField(TEXT("tutorial_completed"), OutProfile.bTutorialCompleted);
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

bool SarkoBackend::ShouldDropTokenOnResponse(bool bAuthenticatedRequest, int32 HttpCode, const FString& ErrorCode)
{
	if (!bAuthenticatedRequest || HttpCode != 401)
	{
		return false;
	}
	// The raid's own session token, not the Bearer token — see the header.
	return ErrorCode != TEXT("bad_session_token");
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
	//
	// ProjectSavedDir() is per *project*, not per process: two `-game` instances
	// launched from this same folder read the same file and therefore authenticate
	// as one player against one stash. Harmless for the listen-server slice, but
	// testing two independent players on one machine needs separate -userdir values
	// (or project copies) rather than two windows.
	//
	// WARNING for headless runs: do NOT pass `-csvCaptureFrames` on the command
	// line. FCsvProfiler::PreInit runs *before* the project path is resolved and
	// poisons FPaths::ProjectSavedDir(), so this function returns the engine's own
	// saved directory instead — ~/Library/Application Support/Epic/UnrealEngine/5.8/
	// Saved/SarkoDevice.txt — and the run silently authenticates as a *different*
	// player with a different stash and a different tutorial flag. Nothing logs it;
	// the raid just looks wrong. The same flag's trace screenshot channel also
	// swallows `Shot showui`, so the evidence you were capturing does not appear
	// either. Start the profiler after init instead: -ExecCmds="CsvProfile Start".
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

	/**
	 * Below this, a deadline-derived clock is not believable for a map that asks
	 * for minutes. RAID_TTL on the deployed service is longer than any shipped
	 * map's duration, so the deadline cannot legitimately leave under two minutes
	 * on a 15-minute map — the likely cause is this machine's clock, not the
	 * server's configuration.
	 */
	constexpr float SanePlayableSeconds = 120.f;

	const float FromMap = MapDurationSeconds > 0.f
		? MapDurationSeconds
		: GetDefault<USarkoRaidSettings>()->RaidDurationSeconds;

	const float FromServer = static_cast<float>(SecondsUntilDeadline) - FMath::Max(0.f, GraceMarginSeconds);

	// Clock skew, not a short session. SecondsUntilDeadline was measured with this
	// machine's FDateTime::UtcNow, so a local clock running minutes fast shrinks it
	// with nothing wrong on the server; obeying it would floor a 15-minute raid to
	// 30 seconds while the caller's Warning blamed RAID_TTL for it. Naming both
	// numbers is the point — the two of them together are what identify skew.
	if (FromServer < SanePlayableSeconds && FromMap > SanePlayableSeconds)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoBackend: the server's deadline leaves only %.0fs while the map asks for %.0fs — implausible for RAID_TTL, so this machine's clock is probably skewed against the server's; using the map's %.0fs"),
			FromServer, FromMap, FromMap);
		return FromMap;
	}

	// The map is the ceiling and the server is the other ceiling; the floor stops
	// a bad clock or an old deadline from producing a raid that is already over.
	// Still honest when the deadline is genuinely short — the RAID_TTL=12m shape
	// clamps to deadline − margin, well above the bound above.
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

TArray<FSarkoItemStack> SarkoBackend::WireLoadout()
{
	// Empty, and that is the fix rather than an omission. /v1/raid/start debits
	// whatever is listed here, and the only credit back is the raid *result*, which
	// carries the backpack alone — so a pistol and 60 rounds walked out of the
	// stash on raid 1 and never returned, and raid 2 answered 409
	// insufficient_items forever after. domain.ValidateStacks accepts an empty
	// list, so this is a legal request and not a loophole.
	//
	// Refill this — and start crediting survivors' equipment back in the result —
	// when weapons and ammo are real in-raid items that can actually be lost.
	return TArray<FSarkoItemStack>();
}

void FSarkoBackendClient::Send(const TCHAR* Verb, const FString& Path, const FString& Body, bool bAuthenticated,
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
	Request->SetVerb(Verb);
	if (!Body.IsEmpty())
	{
		// No Content-Type and no body on a GET: some proxies reject a GET that
		// declares a JSON body, and Go's http.Server will happily read one and
		// then ignore it, which makes a mistake here invisible.
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Body);
	}
	if (bAuthenticated)
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Jwt);
	}
	// Bounded, because a stalled request must not hold the end of a raid open.
	Request->SetTimeout(Settings.BackendTimeoutSeconds);

	// Weak self: an HTTP completion routinely fires after the world — and this
	// client with it — has been torn down.
	TWeakPtr<FSarkoBackendClient> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindLambda(
		[WeakSelf, Path, bAuthenticated, OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const TSharedPtr<FSarkoBackendClient> Self = WeakSelf.Pin();
			if (!Self)
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

				// A rejected token must not outlive the request that was rejected.
				// One client and one JWT now ride the whole launch (the game
				// instance owns them), so keeping a token the server has refused
				// makes every later request fail forever — a rotated JWT_SECRET or
				// a dropped player row used to heal itself on the next raid and
				// silently stopped doing so. Dropped rather than refreshed here:
				// the next caller already asks IsAuthenticated() first
				// (ASarkoShelterPlayerController::FetchProfile,
				// ASarkoRaidGameMode::BeginBackendSession), so re-auth happens on
				// its own, exactly once, with no retry loop to get wrong.
				if (SarkoBackend::ShouldDropTokenOnResponse(bAuthenticated, Code, Parsed.Code))
				{
					Self->Jwt.Reset();
					UE_LOG(LogTemp, Warning,
						TEXT("SarkoBackend: the token was refused, so it has been dropped — the next request will authenticate again"));
				}
				OnComplete(false, ResponseBody, Error);
				return;
			}

			OnComplete(true, ResponseBody, FString());
		});

	if (!Request->ProcessRequest())
	{
		// Logged, but *not* called back: in UE 5.8 a false return means the request
		// was already finished through FinishRequestNotInHttpManager, which fired
		// OnProcessRequestComplete synchronously — the lambda above has run with
		// bConnected=false and OnComplete has already been invoked. Calling it again
		// here would double-fire every handler: two offline-fallback logs, and on the
		// result path two SubmitResult attempts for one raid.
		UE_LOG(LogTemp, Error,
			TEXT("SarkoBackend: %s: the request could not be dispatched (the completion above is that failure)"),
			*Path);
	}
}

void FSarkoBackendClient::Authenticate(FOnDone OnDone)
{
	TWeakPtr<FSarkoBackendClient> WeakSelf = AsShared();
	Send(TEXT("POST"), TEXT("/v1/auth/anonymous"), SarkoBackend::MakeAnonymousBody(SarkoBackend::EnsureDeviceId()),
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
	Send(TEXT("POST"), TEXT("/v1/raid/start"), SarkoBackend::MakeRaidStartBody(MapId, Loadout), /*bAuthenticated*/ true,
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
	Send(TEXT("POST"), TEXT("/v1/raid/confirm"), SarkoBackend::MakeSessionBody(Session.SessionId, Session.SessionToken),
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
	Send(TEXT("POST"), TEXT("/v1/raid/result"),
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

void FSarkoBackendClient::FetchProfile(FOnProfile OnDone)
{
	Send(TEXT("GET"), TEXT("/v1/profile"), FString(), /*bAuthenticated*/ true,
		[OnDone](bool bSuccess, const FString& Body, const FString& Error)
		{
			if (!bSuccess)
			{
				OnDone(false, FSarkoProfile(), Error);
				return;
			}
			FSarkoProfile Profile;
			FString ParseError;
			if (!SarkoBackend::ParseProfileResponse(Body, Profile, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("SarkoBackend: %s"), *ParseError);
				OnDone(false, FSarkoProfile(), ParseError);
				return;
			}
			// The stash is logged by size, not by contents: it is the player's own
			// inventory and there is no reason for it to be in a log file, and on a
			// long-lived stash it would be dozens of lines every shelter entry.
			UE_LOG(LogTemp, Display,
				TEXT("SarkoBackend: profile for %s — %d stash rows, tier '%s', tutorial %s"),
				*Profile.PlayerId, Profile.Stash.Num(), *Profile.VehicleTier,
				Profile.bTutorialCompleted ? TEXT("completed") : TEXT("PENDING"));
			OnDone(true, Profile, FString());
		});
}
