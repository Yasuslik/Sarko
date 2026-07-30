#pragma once

#include "CoreMinimal.h"

#include "Loot/SarkoItemCatalog.h"

#include "SarkoBackendClient.generated.h"

/** What /v1/raid/start hands back. Field names match the Go struct exactly. */
USTRUCT()
struct FSarkoRaidSession
{
	GENERATED_BODY()

	UPROPERTY()
	FString SessionId;

	/**
	 * One-time plaintext token. Returned once and never again — the backend
	 * stores only its SHA-256 hash — so losing it means losing the raid's
	 * result, and it is never logged.
	 */
	UPROPERTY()
	FString SessionToken;

	/**
	 * The raid seed, wrapped into int32 (see SeedToInt32). The backend produces
	 * it as int64(rand.Uint32()), so this is routinely negative.
	 */
	UPROPERTY()
	int32 Seed = 0;

	UPROPERTY()
	FDateTime ExpiresAt = FDateTime(0);
};

/**
 * The outcome enum lives on the game state. Declared here rather than included,
 * so this header stays free of the game framework and can be included from a
 * test that has no world. Forward-declared at global scope, never as an
 * elaborated type inside the namespace below — that mistake creates a second,
 * permanently incomplete SarkoBackend::ESarkoRaidOutcome (see the comment in
 * Core/SarkoRaidGameState.h).
 */
enum class ESarkoRaidOutcome : uint8;

/** The `{"error":{"code","message"}}` shape every failing endpoint returns. */
USTRUCT()
struct FSarkoBackendError
{
	GENERATED_BODY()

	UPROPERTY()
	FString Code;

	UPROPERTY()
	FString Message;
};

namespace SarkoBackend
{
	// ---- pure: request bodies -------------------------------------------------
	// Built by hand rather than through FJsonObjectConverter so the exact field
	// names are visible in this file and cannot drift with a struct rename.

	FString MakeAnonymousBody(const FString& DeviceId);
	FString MakeRaidStartBody(const FString& MapId, const TArray<FSarkoItemStack>& Loadout);
	FString MakeSessionBody(const FString& SessionId, const FString& SessionToken);

	/**
	 * Outcome is the literal string the backend accepts: "extracted" or "died".
	 * Stacks with a non-positive quantity are dropped, because the backend
	 * rejects the whole request for one bad stack and losing a haul to a stray
	 * zero would be absurd.
	 */
	FString MakeRaidResultBody(const FString& SessionId, const FString& SessionToken,
		const FString& Outcome, const TArray<FSarkoItemStack>& Items);

	// ---- pure: responses ------------------------------------------------------

	bool ParseAnonymousResponse(const FString& Json, FString& OutPlayerId, FString& OutToken, FString& OutError);
	bool ParseRaidStartResponse(const FString& Json, FSarkoRaidSession& OutSession, FString& OutError);

	/** Reads `{"expires_at": "<RFC3339>"}` — the confirm response. */
	bool ParseExpiresAtResponse(const FString& Json, FDateTime& OutExpiresAt, FString& OutError);

	/** True when the body is an error envelope. Never treats a success body as an error. */
	bool ParseErrorResponse(const FString& Json, FSarkoBackendError& OutError);

	/**
	 * Wraps the backend's seed into int32, preserving every bit.
	 *
	 * StartRaid returns `int64(rand.Uint32())`, so values above INT32_MAX arrive
	 * routinely (3402905197 was observed live). Assigning that to an int32 is
	 * implementation-defined, and FCString::Atoi saturates — either way the
	 * client's seed stops matching the server's and every container rolls
	 * differently on the two machines. Going through uint32 is well-defined and
	 * lossless.
	 */
	int32 SeedToInt32(int64 Seed);

	/**
	 * Returns this install's device id, creating and persisting one on first run
	 * under Saved/SarkoDevice.txt.
	 *
	 * A GUID string, 36 characters, comfortably inside the backend's 128-char
	 * cap. Persisted rather than derived from hardware so it survives an OS
	 * update and never identifies the machine.
	 */
	FString EnsureDeviceId();

	/** Absolute path of the device-id file. Exposed so a test can reason about it. */
	FString DeviceIdFilePath();

	// ---- pure: the raid loop's own decisions ----------------------------------

	/**
	 * How long the in-raid clock should run, given the map's own duration and the
	 * server's deadline.
	 *
	 * The server's deadline is RAID_TTL + GRACE_BUFFER from confirm time, and
	 * sarko-api/README.md is explicit that the buffer is slack for a slow result
	 * submission rather than play time: a player who extracts on the last second
	 * of a clock that runs to the deadline can lose the whole run to latency. So
	 * the clock is min(map duration, deadline − margin), with a floor so a stale
	 * or already-expired deadline never ends the raid on the spawn frame.
	 *
	 * With RAID_TTL=20m on the deployed service the map's 15 minutes is always the
	 * smaller of the two, so this is a safety net that normally does not bite —
	 * the game mode logs at Warning on the frame it ever does.
	 */
	float ClockSecondsFromDeadline(float MapDurationSeconds, double SecondsUntilDeadline, float GraceMarginSeconds);

	/**
	 * The literal outcome string /v1/raid/result accepts. Exactly "extracted" or
	 * "died" (domain.IsValidOutcome); MIA is death (spec §4.5), and anything
	 * unexpected degrades to "died" — the direction that cannot grant loot for
	 * free.
	 */
	const TCHAR* OutcomeToWire(ESarkoRaidOutcome Outcome);

	/**
	 * What the raid takes in. Must be a subset of what the backend's starter kit
	 * grants, or a new player's first /v1/raid/start is 409 insufficient_items:
	 * the loadout is debited at entry and a new player owns nothing else.
	 *
	 * The medkit stays in the stash: this slice has no healing item to use, so
	 * taking it in would only risk losing it.
	 */
	TArray<FSarkoItemStack> StarterLoadout();
}

/**
 * The client's side of sarko-api.
 *
 * Owned by the raid game mode (the server, in this slice's listen-server
 * topology) and shared, because an HTTP completion can fire after the world has
 * torn down: every callback captures a TWeakPtr to this object and a
 * TWeakObjectPtr to whatever UObject it wants to touch, and does nothing if
 * either is gone.
 *
 * **Offline degradation is a feature (spec §4.6):** every failure logs at Error
 * and calls back with success=false. The raid still plays; nothing persists.
 * The game must never hard-lock on the network, because the developer plays it
 * on a laptop and the player plays it in a lift.
 */
class FSarkoBackendClient : public TSharedFromThis<FSarkoBackendClient>
{
public:
	/** Called with success and, on failure, a human-readable reason already logged. */
	using FOnDone = TFunction<void(bool bSuccess, const FString& Error)>;
	using FOnSession = TFunction<void(bool bSuccess, const FSarkoRaidSession& Session, const FString& Error)>;
	using FOnDeadline = TFunction<void(bool bSuccess, const FDateTime& ExpiresAt, const FString& Error)>;

	bool IsAuthenticated() const { return !Jwt.IsEmpty(); }

	/** POST /v1/auth/anonymous with the persisted device id. */
	void Authenticate(FOnDone OnDone);

	/** POST /v1/raid/start. Debits the loadout. */
	void StartRaid(const FString& MapId, const TArray<FSarkoItemStack>& Loadout, FOnSession OnDone);

	/** POST /v1/raid/confirm. Until this lands the loadout comes back after PENDING_TTL. */
	void ConfirmRaid(const FSarkoRaidSession& Session, FOnDeadline OnDone);

	/** POST /v1/raid/result. Idempotent server-side, so a retry is safe. */
	void SubmitResult(const FSarkoRaidSession& Session, const FString& Outcome,
		const TArray<FSarkoItemStack>& Items, FOnDone OnDone);

private:
	/** One place that builds, sends and unwraps a request. */
	void Send(const FString& Path, const FString& Body, bool bAuthenticated,
		TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& Error)> OnComplete);

	FString Jwt;
	FString PlayerId;
};
