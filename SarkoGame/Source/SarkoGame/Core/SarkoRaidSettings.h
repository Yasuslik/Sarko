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
	// FString, not FName, and the reason is a device-only bug that cost an
	// afternoon: FName folds case, so MapId.ToString() came back as "Bridge"
	// once anything registered that spelling, and the loader asked the OS for
	// Data/Maps/Bridge.json. macOS is case-insensitive and answered; iOS is not
	// and did not, so the map silently failed to load, the world was never
	// built, and the pawn fell out of an empty level. A file name must survive
	// round-tripping exactly as authored.
	FString MapId = TEXT("bridge");

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
	 * How many rounds a weapon spawns a raid with. Negative means a full
	 * magazine, which is the answer for every raid that is not teaching.
	 *
	 * Three, and three is a lesson. Auto-reload is gone (spec §3), so a player
	 * who has never pressed the reload button dry-clicks at the first bot and
	 * dies — and today's route offers them nothing to shoot at between the spawn
	 * and the gas-station forecourt, so there is no safe first trigger pull
	 * anywhere. Starting three rounds into an eight-round magazine puts the
	 * button's empty-state pulse at the spawn camp instead, thirty-odd seconds
	 * before the map holds anything that can hurt them. Clamped by
	 * SarkoCombat::StartingRounds, so a value above MagazineSize is a full
	 * magazine rather than a deeper one.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	int32 StartingMagazineRounds = -1;

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
	 * How far the aim thumb must be deflected before the weapon fires (spec §4.2:
	 * "Hold the aim stick past the dead zone to fire").
	 *
	 * Higher than MoveStickDeadZone on purpose, and Sarko.Input.HoldTheAimStickToFire
	 * asserts it: a movement dead zone only has to reject a resting thumb's drift,
	 * but a FIRING one has to reject a deliberate small deflection — re-gripping,
	 * or turning to face a noise — because a shot the player did not mean to take
	 * gives away their position and empties a magazine they were saving.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Combat")
	float AimFireDeadZone = 0.35f;

	/**
	 * The pocket grid, in cells (spec §1.2). 2x2 — always present, never lost
	 * while alive. Two wide is the number that makes the rule: a 3-wide rifle
	 * cannot enter it, so the best weapons are uncarryable without a bag, and
	 * nothing has to explain that because the grid refuses.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	FIntPoint PocketGrid = FIntPoint(2, 2);

	/**
	 * What a worn backpack adds, as its OWN grid (spec §1.2). 4x2 — 4 + 8 = 12
	 * cells, which is exactly the total the previous design used, so sarko-api's
	 * domain.MaxRaidStacks (13 = twelve cells plus the worn bag) does not move.
	 * Raising either dimension without raising MaxRaidStacks makes full hauls get
	 * rejected at result time, fifteen minutes after the mistake.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Loot")
	FIntPoint BackpackGrid = FIntPoint(4, 2);

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

	/**
	 * Hunger and thirst, per minute of raid time (spec §4).
	 *
	 * A fifteen-minute raid costs 37 % hunger and 50 % thirst, which is what
	 * makes the meters something the player watches rather than scenery. Thirst
	 * is the faster of the two on purpose: a survival game in which the only
	 * drink is vodka has said something it did not mean.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float FoodDrainPerMinute = 2.5f;

	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float WaterDrainPerMinute = 3.3f;

	/**
	 * What a raid begins on. 55/45, NOT 100/100, and that is the whole design.
	 *
	 * The character walked to the sector; starting full would mean neither meter
	 * ever crosses its threshold inside fifteen minutes and the mechanic never
	 * reads at all. At 45, thirst crosses 30 % about four and a half minutes in —
	 * on a first playthrough that lands near the gas station, right after the
	 * route has handed the player a drink at the техдвор.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float FoodStartPercent = 55.f;

	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float WaterStartPercent = 45.f;

	/**
	 * The threshold, inclusive, at which a meter starts costing something.
	 *
	 * Never lethal (owner decision): the entire penalty is out-of-combat health
	 * regeneration — one meter low halves it, both low stop it. That is legible,
	 * recoverable, and it cannot kill a player who is out of food fifty seconds
	 * from the extraction.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float SurvivalLowPercent = 30.f;

	/**
	 * Out-of-combat health regeneration, introduced with hunger and thirst
	 * because it is the thing they gate — health has only ever gone down.
	 *
	 * 1.5 hp/s is a full recovery in 66 seconds, which is a real fraction of a
	 * fifteen-minute raid and about the length of the walk home: healing up costs
	 * you the walk rather than being free. It also cannot be felt in a fight —
	 * a scav at 28 damage every 1.3 s is 21.5 hp/s, fourteen times this — so the
	 * medkit stays the in-combat answer, which is exactly the point.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float HealthRegenPerSecond = 1.5f;

	/**
	 * How long after the last damage taken OR dealt regeneration may start.
	 *
	 * Eight seconds is longer than a scav's whole engagement, so nothing about
	 * this is ever felt mid-fight; it is what turns "I got shot" into a cost that
	 * is paid by walking somewhere quiet.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float HealthRegenDelaySeconds = 8.f;

	/** How often the survival meters tick. Four times a second: they move by
	 *  hundredths of a percent and have nothing to say at 60 Hz. */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float SurvivalTickIntervalSeconds = 0.25f;

	/** A bottle of water. Half a meter in one 1x1 cell — worth carrying, not
	 *  worth carrying five of. */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float WaterBottleRestoresWater = 50.f;

	/** A can. Slightly less than the bottle gives of water, because hunger drains
	 *  slower and one can should not cover a whole raid. */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float CannedFoodRestoresFood = 45.f;

	/**
	 * Vodka: a small heal with a cost, which is the traditional shape and the
	 * honest one.
	 *
	 * It is the only heal in the game that is not a medkit, and it is worse than
	 * one in every way that matters — 15 hp against a medkit's full answer, and
	 * it takes 15 points of water with it, because alcohol is a diuretic and
	 * because a drink that helped your thirst would make the water bottle
	 * pointless. Drinking your way out of a firefight is therefore a decision
	 * with a bill attached, and the bill arrives on the walk home.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float VodkaHeals = 15.f;

	UPROPERTY(EditAnywhere, config, Category = "Survival")
	float VodkaCostsWater = 15.f;

	/**
	 * THE NOISE MODEL (spec §7). What each thing a pawn can do carries, in uu.
	 *
	 * These replaced EnemyHearingRadiusUU, which was a property of the LISTENER
	 * and therefore could not be spent by the player: inside it you were heard,
	 * outside it you were not, whatever you did. A radius that belongs to the
	 * *event* is a radius the player chooses — walk and almost nothing reaches;
	 * fire and the whole POI knows.
	 *
	 * 2600 for a shot is deliberately the widest number in the AI's vocabulary
	 * and wider than the sector's post spacing: a gunshot SHOULD be able to reach
	 * two encounters' posts. That is not a leak in the "one bot in the first
	 * fight" law — encounters gate SPAWNING (SarkoEncounter::AllowedSpawnCount and
	 * the budget), noise gates only the behaviour of bots that already exist, and
	 * nothing in this file can create one.
	 *
	 * 1100 for a run matches the distance a bot will open fire from, so choosing
	 * to sprint across open ground is choosing to be heard by anything that could
	 * already have shot you. 450 for a walk is barely more than the interact
	 * radius: walking past a guarded crate is genuinely quiet, which is the whole
	 * verb this stage adds.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float NoiseLoudRadiusUU = 2600.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float NoiseAudibleRadiusUU = 1100.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float NoiseQuietRadiusUU = 450.f;

	/**
	 * Fraction of MaxWalkSpeed at which moving becomes RUNNING, and below which
	 * it stops being moving at all.
	 *
	 * The move stick's deflection already scales speed (SarkoAim::MoveIntentScale
	 * feeds AddMovementInput), so a fraction of top speed IS a stick deflection —
	 * and the server reads it off its own copy of the pawn's velocity rather than
	 * off anything a client sends. 0.7 leaves a wide, findable band of "walking":
	 * a thumb has to be deliberately short of the ring's edge, which is exactly
	 * the input a player makes when they are being careful.
	 *
	 * NoiseMoveSpeedFraction is the same idea as MoveStickDeadZone, one layer
	 * down: a pawn braking to a stop or sliding on a slope must not go on
	 * announcing itself for the second the deceleration takes.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float NoiseRunSpeedFraction = 0.7f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float NoiseMoveSpeedFraction = 0.15f;

	/**
	 * How long an event stays hearable. Longer than NoiseMovementIntervalSeconds
	 * on purpose, so a pawn that keeps moving is heard continuously rather than
	 * blinking in and out of earshot between reports.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float NoiseEventLifetimeSeconds = 0.75f;

	/**
	 * How often a moving pawn reports where it is. Not every tick: the ring
	 * buffer would churn sixty entries a second to say the same thing, and the
	 * bot investigating a footstep from 0.25 s ago walks to a point 100 uu from
	 * the one it would have walked to.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float NoiseMovementIntervalSeconds = 0.25f;

	/**
	 * The fallback listener sensitivity, for a bot with no archetype (the map's
	 * plain `botSpawns`). Every encounter-spawned bot gets its own from
	 * SarkoAI::GetBotArchetypes through ASarkoAIController::SetPerception.
	 *
	 * A multiplier and not a radius: how far a noise carries is a property of the
	 * noise now, and how well a bot listens is a property of the bot.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyHearingSensitivity = 1.f;

	/**
	 * How far a bot can SEE, line of sight permitting.
	 *
	 * New with the noise model, and not new behaviour: EnemyHearingRadiusUU used
	 * to bound sight as a side effect, because DecideState refused to react to
	 * anything outside it whether or not the bot could see it. Deleting hearing
	 * without replacing that bound would have given every bot map-wide vision.
	 *
	 * 1600 uu is the landscape camera's measured lateral half-view (see
	 * EncounterMinSpawnDistanceUU's note): a bot sees you at about the distance
	 * you can see it, and it still may not shoot until EnemyFiringRangeUU.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemySightRangeUU = 1600.f;

	/**
	 * Health at or below which the screen-edge vignette appears (spec §4).
	 *
	 * Thirty of a hundred is two of a scav_pistol's hits from dead. Drawn rather
	 * than a material (there is no post-process asset in this project and there
	 * will not be one), and drawn UNDER every readout, so it can never be the
	 * reason the survival meters became hard to read.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Feedback")
	float LowHealthVignetteHealth = 30.f;

	/**
	 * How long a directional damage arc stays on screen (spec §4).
	 *
	 * Long enough to read and turn toward, short enough that three hits in a
	 * fight are three arcs and not a ring. The camera is world-locked, so the
	 * direction a hit came from is information the player has no other way to get.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Feedback")
	float DamageArcSeconds = 0.6f;

	/** How long the hit marker sits over the victim. A confirmation, not a decoration. */
	UPROPERTY(EditAnywhere, config, Category = "Feedback")
	float HitMarkerSeconds = 0.15f;

	/** How long an enemy body flashes white when it takes a hit. */
	UPROPERTY(EditAnywhere, config, Category = "Feedback")
	float HitFlashSeconds = 0.1f;

	/**
	 * How far an enemy may open fire, in unreal units. Its own setting since the
	 * realism stage; it used to be `WeaponRangeUU * 0.5` in the controller, i.e.
	 * 2000 uu against a screen that is about 1090 uu across at the pawn's depth,
	 * so the very first shot of a raid arrived from a bot the player could not
	 * see. ТЗ §11 forbids exactly that. 1100 sits inside the ~1380 uu of forward
	 * view the portrait camera gives, so a fight starts on screen or not at all.
	 *
	 * A separate number from the player's WeaponRangeUU on purpose: how far a
	 * bullet travels and how far a bot is willing to shoot from are two
	 * questions, and tying them together is what made this one invisible.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyFiringRangeUU = 1100.f;

	/**
	 * How far from its post a bot may wander when it has nothing to react to,
	 * for a bot whose map data authored no leash of its own.
	 *
	 * Before this existed, PatrolTarget initialised to FVector::ZeroVector (the
	 * world origin, which on this map is the closed bridge) and RerollPatrolTarget
	 * picked uniformly inside +/-MapExtent*0.8 — a 320x320 m square. With the
	 * stuck detector firing every 2 s, every authored bot post was fiction within
	 * about 90 seconds and the map's whole placement design with it.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIPatrolLeashUU = 1400.f;

	/**
	 * How close to the remembered noise an investigating bot has to get before it
	 * counts as "looked, found nothing" and walks back to its post.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIInvestigateArriveRadiusUU = 250.f;

	/**
	 * How long a bot keeps walking toward a noise it never found. Without a
	 * bound, a player who breaks line of sight and keeps moving would be followed
	 * across the sector by a bot that renews its memory every tick — which is
	 * chase-through-walls again, wearing the Investigate name.
	 */
	UPROPERTY(EditAnywhere, config, Category = "AI")
	float AIInvestigateTimeoutSeconds = 12.f;

	UPROPERTY(EditAnywhere, config, Category = "AI")
	float EnemyFireIntervalSeconds = 0.9f;

	/**
	 * How often the server evaluates encounter triggers. Not per tick: six
	 * triggers at 4 Hz is a handful of distance comparisons a second, and the
	 * player cannot move far enough in 0.25 s for the difference to be felt.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Encounters")
	float EncounterEvaluationIntervalSeconds = 0.25f;

	/**
	 * The hard floor between an encounter's spawn point and the player at the
	 * instant an enemy is created.
	 *
	 * 2600, and the 1800 it replaces was calibrated against a camera that no
	 * longer exists. The old comment here read "the PORTRAIT camera (a 1400 uu
	 * boom at -70 degrees) shows about 1380 uu ahead of the pawn and 545 uu to
	 * either side" — 545 uu of lateral half-view. The game runs LANDSCAPE, where
	 * the same boom and the same pitch put the half-view at roughly 1570 uu: not
	 * three times inside the old floor but very nearly ON it. A bot appearing
	 * 1800 uu away and 30 degrees off the walking direction was, measurably, a
	 * bot appearing on screen — the one thing a game with a handful of enemies
	 * per raid can never be forgiven for.
	 *
	 * 2600 is 1.66x the measured landscape half-view, which restores the margin
	 * the 1800 was believed to have. It costs the authoring something real and
	 * that is the point: an encounter whose only door is 2000 uu from the trigger
	 * now defers instead of firing in view, which is visible in the log as "gave
	 * up after Ns" and is the correct outcome.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Encounters")
	float EncounterMinSpawnDistanceUU = 2600.f;

	/**
	 * How long an armed encounter keeps waiting for an authored spawn point to
	 * become usable before it gives the attempt up and re-arms on the next
	 * approach. It never relocates a spawn toward the player and never spawns in
	 * view — deferring is the only move it has.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Encounters")
	float EncounterSpawnDeferSeconds = 5.f;

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
