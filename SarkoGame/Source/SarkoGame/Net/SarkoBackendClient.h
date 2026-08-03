#pragma once

#include "CoreMinimal.h"

// Included, not forward-declared: FSarkoEquipment is a USTRUCT held by value on
// FSarkoProfile below.
#include "Loot/SarkoEquipment.h"
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

	/**
	 * "raid" or "sortie", echoed by the server (spec §4.5).
	 *
	 * Read rather than assumed, because the client's ASK and the server's ANSWER are
	 * different facts: a service older than the field, or one that refused the mode,
	 * answers "raid" for a request that said "sortie". A client that assumed its own
	 * ask would then show borrowed gear it was never lent.
	 *
	 * A string and not an enum, for the same reason FSarkoProfile::VehicleTier is one:
	 * the client only ever compares and displays it, and an unknown future mode must
	 * not fail a parse.
	 */
	UPROPERTY()
	FString Mode;

	/**
	 * What a ВИЛАЗКА lent this player — the SERVER's choice, and the only place the
	 * client learns it.
	 *
	 * Empty for an ordinary raid, and empty is not a failure: the field is absent from
	 * a raid's response on purpose (`granted_kit,omitempty`), so its presence is
	 * itself the signal that the server chose the loadout.
	 *
	 * NOTHING IS DERIVED FROM IT except what is drawn. It is not sent back, not
	 * merged into the profile's equipment, and not used to decide the in-raid gun:
	 * the stash credit on extraction is computed server-side from the session row, so
	 * a client that lost or mangled this list still gets exactly what it was lent.
	 */
	UPROPERTY()
	TArray<FSarkoItemStack> GrantedKit;

	/** A ВИЛАЗКА rather than an ordinary raid. One place asks the string, so a
	 *  spelling can only be wrong once. */
	bool IsSortie() const { return Mode == TEXT("sortie"); }
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

	/**
	 * What the player is wearing — the `equipment` object, `{"weapon":"pistol"}`.
	 *
	 * It rides on the profile because it is player state that outlives every raid,
	 * and because the ІНВЕНТАР screen draws the character and the stash from ONE
	 * fetch: two fetches would let the two halves of that screen disagree, which
	 * for a stash that has just been debited by a raid start is a screen showing an
	 * equipped pistol beside a stash that no longer has one.
	 *
	 * Optional on the wire, and an absent object parses as "wearing nothing" —
	 * the same direction bTutorialCompleted takes, so a backend older than the
	 * field degrades to an unarmed player rather than failing the parse and
	 * leaving the shelter offline.
	 */
	UPROPERTY()
	FSarkoEquipment Equipment;

	/**
	 * How many seconds until this player may take another ВИЛАЗКА, or 0 for "now"
	 * (spec §4.5).
	 *
	 * A NUMBER TO DRAW, and nothing else. The client displays it on the second
	 * button; it does not decide with it, does not tick it down into a decision, and
	 * does not gate the request on it — /v1/raid/start refuses a sortie inside the
	 * cooldown by name (`sortie_cooldown`) however stale this value has become, which
	 * is what makes the countdown a label rather than a rule.
	 *
	 * Absent parses as 0, i.e. "available". That is the direction that offers a
	 * button the server may then refuse, which costs one round trip and a status
	 * line; the other direction would hide the recovery path from a player who needs
	 * it because their backend is a version behind.
	 */
	UPROPERTY()
	int32 SortieCooldownSeconds = 0;
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
	/**
	 * POST /v1/raid/start's body.
	 *
	 * Mode is the literal the backend accepts — "raid" or "sortie"
	 * (domain.IsValidRaidMode) — and it is the ONLY thing this client says about a
	 * free run. There is deliberately no parameter for a kit and none for a
	 * cooldown: both are the server's, and a body that could carry them is a body
	 * that could be forged into a better one.
	 *
	 * An EMPTY Mode omits the field entirely rather than sending `"mode":""`, so a
	 * request built the way it always was is byte-identical to the one it always was.
	 */
	FString MakeRaidStartBody(const FString& MapId, const TArray<FSarkoItemStack>& Loadout,
		const FString& Mode = FString());
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
	 * What the raid takes in on the wire: **the equipped items** (spec §4).
	 *
	 * It was deliberately EMPTY until 2026-08-03, and the reason it could not stay
	 * empty is the reason it was empty: /v1/raid/start debits the loadout and only
	 * the raid result credited anything back, and the result carries the backpack
	 * alone — so a non-empty loadout used to be a one-way withdrawal that spent the
	 * starter kit on raid 1 and answered 409 insufficient_items forever after. The
	 * missing half is now built: an EXTRACTION credits the session's recorded
	 * loadout back server-side, so the debit is reversible and the withdrawal has a
	 * matching deposit.
	 *
	 * A raid still begins with an empty BAG. Equipment is worn, not packed — the
	 * server returns it on extraction rather than the client submitting it as haul
	 * — so the carry grid, the magazine fold-back and domain.FitsCarryGrid are all
	 * untouched by this.
	 *
	 * An empty result is still perfectly legal, and that is load-bearing:
	 * domain.ValidateStacks accepts an empty list, and a player with nothing
	 * equipped must be able to raid (spec §4's dead-end guard).
	 */
	TArray<FSarkoItemStack> WireLoadout(const FSarkoEquipment& Equipment,
		const FSarkoItemCatalog& Catalog);

	/** POST /v1/profile/equipment's body. An empty item id is the unequip, not an
	 *  omission: "put nothing in this slot". */
	FString MakeSetEquipmentBody(ESarkoEquipSlot Slot, FName Item);

	/** Reads `{"equipment":{"weapon":"pistol"}}` — the equip response, which is the
	 *  equipment as it stands after the write, so an optimistic client-side update
	 *  is corrected by the answer rather than by the next profile fetch. */
	bool ParseEquipmentResponse(const FString& Json, FSarkoEquipment& OutEquipment, FString& OutError);
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
	using FOnEquipment = TFunction<void(bool bSuccess, const FSarkoEquipment& Equipment,
		const FString& Error)>;

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

	/**
	 * POST /v1/profile/equipment. One slot per call, because the tap this serves is
	 * always about exactly one slot — a whole-set write would let a client with a
	 * stale idea of one slot silently clear another.
	 *
	 * 409 not_equippable and 409 insufficient_items arrive here as ordinary
	 * failures with the envelope's message in Error, which the shelter shows: a
	 * refused equip with no reason is the failure the refusal discipline exists to
	 * prevent.
	 */
	void SetEquipment(ESarkoEquipSlot Slot, FName Item, FOnEquipment OnDone);

	/**
	 * POST /v1/raid/start. Debits the loadout — unless Mode is "sortie", in which
	 * case the server debits nothing and grants a kit instead (spec §4.5).
	 *
	 * `409 sortie_cooldown` arrives here as an ordinary failure with the envelope's
	 * message in Error, which the shelter shows verbatim: it names the remaining
	 * time, and a refused free run with no reason is the failure the refusal
	 * discipline exists to prevent.
	 */
	void StartRaid(const FString& MapId, const TArray<FSarkoItemStack>& Loadout, FOnSession OnDone,
		const FString& Mode = FString());

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
