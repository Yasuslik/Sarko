#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "SarkoRaidSettings.generated.h"

/**
 * Every tunable for the raid, in one place, editable from Config/DefaultGame.ini.
 * Nothing in gameplay code hardcodes a balance number: the whole point of the
 * slice is to change these quickly while looking for what feels good.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Sarko Raid"))
class USarkoRaidSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** How long a raid runs before the timer expires. */
	UPROPERTY(EditAnywhere, config, Category = "Raid")
	float RaidDurationSeconds = 480.f;

	/**
	 * Half-extent of the square play area, in unreal units (10000 uu = 100 m).
	 * The authored map carries its own extentUU and the geometry comes from
	 * there; this is what code with no map definition in hand reasons about —
	 * the AI's patrol square and the overview camera's framing height — so it
	 * must be kept in step with the map file it is used alongside.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	float MapExtent = 10000.f;

	/** Which file under Data/Maps to load. */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	FName MapId = TEXT("bridge");

	/**
	 * How close the local pawn has to get to a tree before that tree's canopy
	 * hides itself — the one number that makes a walk-in forest playable under a
	 * top-down camera.
	 *
	 * WHERE 1000 COMES FROM. The camera is a 1400 uu boom pitched -70 degrees, so
	 * it sits about 480 uu behind the pawn and 1320 uu above it. The sight line
	 * from there to the pawn passes through canopy height (roughly 300-900 uu)
	 * between 110 and 330 uu behind the pawn's own position; canopies are up to
	 * 300 uu in radius; so anything whose trunk is within ~650 uu can put leaves
	 * between the camera and the player. 1000 uu is that, plus enough margin that
	 * the hole opens BEFORE the player walks into it rather than around them
	 * afterwards — a canopy that pops out at the moment it would have hidden you
	 * reads as a glitch even though it is technically sufficient.
	 *
	 * Bigger is not better: this is a hole cut in the forest roof, and at 2000 uu
	 * the player walks around inside a permanent clearing and the stand stops
	 * reading as a stand at all. Smaller than about 700 and the pawn's own
	 * silhouette starts clipping the edge of the hole.
	 *
	 * Purely cosmetic and purely local. It changes an instance transform on the
	 * machine the player is looking at; it never touches collision, never touches
	 * gameplay state and never leaves the client. Setting it to 0 disables the
	 * effect entirely, which is a fine way to see the problem it solves.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Map")
	float CanopyFadeRadiusUU = 1000.f;

	UPROPERTY(EditAnywhere, config, Category = "Movement")
	float WalkSpeed = 400.f;

	/**
	 * Stick deflection below this magnitude counts as centred. Without a
	 * dead zone, a resting thumb's tiny drift off true-centre would read as
	 * a non-zero move intent and creep the character.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Movement")
	float MoveStickDeadZone = 0.15f;

	/**
	 * Half-angle of the cone inside which a shot snaps to a target. This is a
	 * nudge that compensates for a thumb, not an aimbot — it is applied on the
	 * server, identically for everyone, so it never becomes an advantage.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float AimConeHalfAngleDegrees = 6.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float WeaponRangeUU = 4000.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float WeaponDamage = 22.f;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float ReloadSeconds = 2.2f;

	/**
	 * Minimum server-enforced time between shots from the same weapon,
	 * regardless of how fast fire requests arrive. The enemy already has its
	 * own EnemyFireIntervalSeconds cooldown; this is the equivalent backstop
	 * for the player, since nothing else stops a client from sending fire
	 * requests faster than a human could pull the trigger and emptying the
	 * magazine in one frame.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float MinFireIntervalSeconds = 0.15f;

	/**
	 * Pocket cells, carried always (container-inventory spec §2.3). Four is small
	 * on purpose: it is the number that makes finding a backpack matter, and it is
	 * the number a player has for the first minute of every raid.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	int32 BasePocketCells = 4;

	/**
	 * What a worn backpack adds. 4 + 8 = 12, which is exactly the old fixed
	 * BackpackSlots — so the backend's plausibility cap on a full haul does not
	 * move, and only the *start* of a raid got harder. Raising this without
	 * raising sarko-api's domain.MaxRaidStacks makes full hauls get rejected at
	 * result time, fifteen minutes after the mistake.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	int32 BackpackBonusCells = 8;

	/** How close the pawn must be to open a container. Enforced on the server. */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	float InteractRadiusUU = 250.f;

	/**
	 * Press-and-hold time to open a container (spec §4.3). This is the cost of
	 * looting: standing still for a second and a half beside a crate is what
	 * makes a container a decision rather than a pickup.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	float LootChannelSeconds = 1.5f;

	/**
	 * Seconds the player must stand inside an extraction zone (spec §4.5).
	 *
	 * Slice-1 spec §8 says ten; the Stage A spec says five and is the later
	 * decision, so five it is. This being tunable rather than a constant is the
	 * point: it is the length of the most frightening moment in the raid.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Extraction")
	float ExtractDwellSeconds = 5.f;

	/**
	 * How long the raid's outcome banner stays on screen before the game travels
	 * back to the shelter (spec §6.5).
	 *
	 * Not zero: the player has to see EXTRACTED/KIA/MIA where it happened, in the
	 * place they died or extracted from, before the screen changes. Not long
	 * either — the itemised haul is in the shelter now, so there is nothing to
	 * read here.
	 *
	 * The raid result is submitted independently of this timer, and both the game
	 * instance and the submission lambda hold the backend client, so travelling
	 * mid-request cannot lose the result — only the log line about it moves worlds.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Raid")
	float PostRaidReturnSeconds = 5.f;

	/**
	 * Whether the raid talks to sarko-api at all. Off means the raid runs on a
	 * local seed and nothing persists — useful on a plane, and the only way to
	 * iterate on gameplay while the backend is down.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	bool bBackendEnabled = true;

	/**
	 * Which loot mode an offline raid uses, when there is no profile to ask.
	 *
	 * True (the default) means an offline raid replays the tutorial's static
	 * layout: nothing persists offline, so replaying it costs nothing and gives a
	 * deterministic raid to iterate against. Set it false to exercise the seeded
	 * roll path with the backend off.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	bool bOfflineTutorialLoot = true;

	/**
	 * No trailing slash: paths are appended verbatim.
	 *
	 * In DefaultGame.ini this value must be *quoted*: the ini parser swallows
	 * "//" as a comment, so an unquoted URL loads as "https:" and every request
	 * goes nowhere with no warning at all.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	FString BackendBaseUrl = TEXT("https://sarko-api-production.up.railway.app");

	/**
	 * The map id sent to /v1/raid/start, which is not necessarily the local data
	 * file name.
	 *
	 * sarko-api unlocks maps by vehicle tier (internal/domain/garage.go), and
	 * tier `none` unlocks exactly one map; sending anything else gets 403
	 * map_locked. That map was renamed forest -> bridge in 80a5a4d, so today this
	 * matches MapId — the indirection stays because the wire id belongs to the
	 * backend's ladder and the data-file name belongs to this repo, and they have
	 * already drifted apart once.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	FString BackendMapId = TEXT("bridge");

	/**
	 * How far short of the server's deadline the in-raid clock stops.
	 *
	 * /v1/raid/confirm returns expires_at = now + RAID_TTL + GRACE_BUFFER (22
	 * minutes on the deployed service, RAID_TTL having been raised to 20m so a
	 * 15-minute map plays in full). sarko-api/README.md is explicit that the
	 * grace buffer is slack for a slow submission and not play time, so the
	 * clock ends this many seconds earlier. Playing right up to expires_at means
	 * a player who extracts on the last second can lose the run to network
	 * latency.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	float BackendGraceMarginSeconds = 120.f;

	/** Per-request timeout. Short: a stalled call must not hold the end of a raid open. */
	UPROPERTY(EditAnywhere, config, Category = "Backend")
	float BackendTimeoutSeconds = 10.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyHearingRadiusUU = 2500.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyFireIntervalSeconds = 0.9f;

	/**
	 * Hysteresis band for the Shoot/Chase boundary: once shooting, the enemy
	 * tolerates the target drifting up to this far past the firing range
	 * before giving up and chasing again, instead of re-evaluating against
	 * the bare firing range every tick and chattering between the two
	 * states right at the boundary.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIShootHysteresisRangeUU = 150.f;

	/** Chase/patrol speed. A separate knob from the player's WalkSpeed because
	 * how fast the enemy closes distance is its own balance question. */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyWalkSpeed = 340.f;

	/**
	 * There is no navmesh in this project (the map's actors are spawned from a
	 * data file at runtime rather than authored into a level, so nothing is
	 * baked, and nothing configures runtime generation either) — the enemy
	 * steers directly at its target instead of pathing.
	 * This is how far ahead it casts a single forward trace to decide whether
	 * the straight line to the target is blocked and it needs to steer around
	 * something instead of walking into it.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIAvoidanceTraceDistanceUU = 150.f;

	/**
	 * How sharply the enemy turns away from its target direction when the
	 * avoidance trace reports the way ahead is blocked, in degrees. Applied as
	 * a single fixed rotation of the desired direction rather than a search,
	 * which is enough to curve around one box of cover on a flat plane.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIAvoidanceSteerDegrees = 60.f;

	/**
	 * Fallback steer angle tried on both sides when the narrow
	 * AIAvoidanceSteerDegrees rotation is also blocked. Wider than the
	 * primary angle so a pocket that defeats the first attempt has a real
	 * chance of clearing on the second.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIAvoidanceWideSteerDegrees = 120.f;

	/**
	 * Radius of the capsule swept ahead of the enemy to test for obstacles.
	 * A line trace from the capsule centre reports "clear" even when
	 * geometry clips the pawn's shoulder, since the enemy's own collision
	 * capsule is roughly this wide; sweeping a capsule this size instead
	 * catches that case.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIAvoidanceProbeRadiusUU = 40.f;

	/**
	 * Backstop against any steering failure: if the enemy has not moved more
	 * than this far (in either state — patrol or chase) within
	 * AIStuckTimeThresholdSeconds, it re-rolls PatrolTarget and falls back to
	 * patrolling for that tick, regardless of what the steering math decided.
	 * This guarantees no permanent freeze even if avoidance is defeated by
	 * the geometry.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIStuckDisplacementThresholdUU = 50.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIStuckTimeThresholdSeconds = 2.f;

	/**
	 * Per-second NAVDIAG-style AI logging, off by default because it is noise
	 * from every enemy every tick. Flip on only when actively debugging AI
	 * movement.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	bool bLogAIDiagnostics = false;
};
