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
 * What GET /v1/profile hands back. Field names below are the parser's business;
 * the wire names are exactly `player_id`, `schema_version`, `stash`,
 * `vehicle_tier`, `unlocked_maps`, `tutorial_completed` (store.Profile's JSON
 * tags in sarko-api/internal/store/players.go).
 *
 * Re-fetched on every shelter entry rather than cached across a raid: a level
 * travel destroys every actor, and the whole point of coming back to the shelter
 * is to see what the raid just credited.
 */
USTRUCT()
struct FSarkoProfile
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerId;

	UPROPERTY()
	int32 SchemaVersion = 0;

	/** Ordered by item id, server-side. The shelter draws it in that order. */
	UPROPERTY()
	TArray<FSarkoItemStack> Stash;

	/** `none|bicycle|motorcycle|car|helicopter` — domain.Tier. A string, not an
	 *  enum: the client only displays it, and an unknown future tier must not
	 *  fail a parse. */
	UPROPERTY()
	FString VehicleTier;

	UPROPERTY()
	TArray<FString> UnlockedMaps;

	/**
	 * False until the player's first *successful* raid (spec §6.5). While false,
	 * containers read the map's authored `fixedItems` instead of rolling.
	 *
	 * **Defaults to false, and an absent wire field parses as false**, so both
	 * "brand-new player" and "backend older than the flag" land in tutorial mode
	 * — the direction that shows static loot rather than skipping the tutorial
	 * forever.
	 */
	UPROPERTY()
	bool bTutorialCompleted = false;
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

	/**
	 * Reads the profile. Every field is validated: a stash row with no id or a
	 * non-positive quantity fails the whole parse rather than being skipped,
	 * because a silently-shortened stash is a player being shown items they do
	 * not have — or not being shown items they do.
	 *
	 * `tutorial_completed` is the one optional field: absent means false.
	 */
	bool ParseProfileResponse(const FString& Json, FSarkoProfile& OutProfile, FString& OutError);

	/**
	 * Reads `{"vehicle_tier":"bicycle","unlocked_maps":["bridge","swamp"]}` — the
	 * shape api/garage_handler.go's craftResponse marshals.
	 *
	 * `vehicle_tier` is required and a missing one fails the parse: the whole
	 * point of the call is which vehicle now exists, and an empty string shown to
	 * the player as their garage would be worse than an error. `unlocked_maps` is
	 * optional — it is derived server-side from the tier (domain.UnlockedMaps) and
	 * is only used for one sentence.
	 */
	bool ParseCraftResponse(const FString& Json, FString& OutTier,
		TArray<FString>& OutUnlockedMaps, FString& OutError);

	/** True when the body is an error envelope. Never treats a success body as an error. */
	bool ParseErrorResponse(const FString& Json, FSarkoBackendError& OutError);

	/**
	 * Whether a finished request means this client's JWT is dead and must be
	 * dropped, so the next call authenticates again.
	 *
	 * The whole point is that a rejected token must not outlive the request that
	 * was rejected. Before the game instance existed every raid authenticated from
	 * scratch, so a rotated JWT_SECRET or a deleted player row healed itself on the
	 * next trip; now one client and one token ride the whole launch, and a token
	 * that is kept after a 401 makes *every* later request fail with nothing to
	 * explain it but a first 401 scrolled off the top of the log.
	 *
	 * `bad_session_token` is the one 401 that must **not** drop it: that code comes
	 * from /v1/raid/result comparing the raid's own session token
	 * (api/raid_handler.go), which means the Bearer token in front of it was
	 * accepted — dropping it there would burn a re-auth on a fault it cannot fix.
	 * `unauthorized` is what auth/middleware.go answers for a bad Bearer token.
	 *
	 * Any other 401 — including one whose body is not this backend's error envelope
	 * at all, such as a proxy's own — drops the token too. One extra anonymous auth
	 * is a single cheap round trip; a token that is permanently dead and never
	 * replaced costs the player every raid until they relaunch.
	 *
	 * ErrorCode is the parsed envelope's `code`, or empty when the body was not an
	 * envelope.
	 */
	bool ShouldDropTokenOnResponse(bool bAuthenticatedRequest, int32 HttpCode, const FString& ErrorCode);

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
	 *
	 * It identifies an *install*, not a process: the file lives in this project's
	 * Saved/ directory, so two `-game` instances launched from the same project
	 * folder on one machine authenticate as the same player and share one stash.
	 * That is fine for a listen-server test but wrong for testing two independent
	 * players locally — that needs separate -userdir/project copies, or a
	 * per-instance override of this id.
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
	 *
	 * SecondsUntilDeadline is measured against *this machine's* clock, so it also
	 * absorbs clock skew. A local clock minutes ahead of the server's would floor a
	 * 15-minute raid to the 30-second minimum while every log line blamed RAID_TTL,
	 * so an implausibly short deadline (see the sane lower bound inside) is treated
	 * as skew: the map's duration wins and the Warning names both numbers. A
	 * deadline that is merely *short* — the RAID_TTL=12m shape — is still clamped
	 * honestly.
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
	 * What the raid takes in on the wire. **Empty, on purpose** — see the comment
	 * at the /v1/raid/start call site in SarkoRaidGameMode::BeginBackendSession.
	 *
	 * /v1/raid/start debits the loadout from the stash and only the raid *result*
	 * credits anything back, and the result carries the backpack alone. So any
	 * non-empty loadout here is a one-way withdrawal: raid 1 spends the starter
	 * kit, raid 2 gets 409 insufficient_items, and the client falls offline
	 * permanently. Nothing in the raid reads the loadout anyway — the weapon is
	 * abstract with infinite reloads — so debiting for it was risk without stakes.
	 *
	 * domain.ValidateStacks explicitly allows an empty list. This function stays
	 * as the named seam to fill in when weapons and ammo become real in-raid
	 * items and losing them on death is the actual stake.
	 */
	TArray<FSarkoItemStack> WireLoadout();
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
	using FOnProfile = TFunction<void(bool bSuccess, const FSarkoProfile& Profile, const FString& Error)>;
	using FOnCraft = TFunction<void(bool bSuccess, const FString& Tier,
		const TArray<FString>& UnlockedMaps, const FString& Error)>;

	bool IsAuthenticated() const { return !Jwt.IsEmpty(); }

	/** POST /v1/auth/anonymous with the persisted device id. */
	void Authenticate(FOnDone OnDone);

	/** GET /v1/profile. The only GET this client makes. */
	void FetchProfile(FOnProfile OnDone);

	/**
	 * POST /v1/garage/craft. **No request body** — the server reads the player id
	 * from the JWT and crafts the NEXT tier itself (store.CraftNextVehicle); a
	 * client that named a tier would be naming one it does not get to choose.
	 *
	 * 409 insufficient_items and 409 max_tier arrive here as ordinary failures
	 * with the envelope's message in Error, which the shelter shows verbatim: the
	 * only thing worse than a refused craft is a refused craft with no reason.
	 */
	void CraftVehicle(FOnCraft OnDone);

	/** POST /v1/raid/start. Debits the loadout. */
	void StartRaid(const FString& MapId, const TArray<FSarkoItemStack>& Loadout, FOnSession OnDone);

	/** POST /v1/raid/confirm. Until this lands the loadout comes back after PENDING_TTL. */
	void ConfirmRaid(const FSarkoRaidSession& Session, FOnDeadline OnDone);

	/** POST /v1/raid/result. Idempotent server-side, so a retry is safe. */
	void SubmitResult(const FSarkoRaidSession& Session, const FString& Outcome,
		const TArray<FSarkoItemStack>& Items, FOnDone OnDone);

private:
	/**
	 * One place that builds, sends and unwraps a request.
	 *
	 * Verb is explicit rather than inferred from "is the body empty": GET
	 * /v1/profile has no body, and a POST with an empty body is a legitimate
	 * shape too, so inferring it would make the two indistinguishable.
	 */
	void Send(const TCHAR* Verb, const FString& Path, const FString& Body, bool bAuthenticated,
		TFunction<void(bool bSuccess, const FString& ResponseBody, const FString& Error)> OnComplete);

	FString Jwt;
	FString PlayerId;
};
