#pragma once

#include "CoreMinimal.h"

/**
 * The encounter system's decisions, as pure functions of what the server knows.
 *
 * Everything here is arithmetic: no world, no actors, no timers. The game mode
 * owns the timer, the traces and the SpawnActor calls; this owns the rules, so
 * "the budget is the law" and "nothing spawns in view" are unit-testable
 * statements rather than things the code is believed to do.
 *
 * All of it is server-only by construction — nothing in this header is
 * replicated, and the budget in particular must never reach a client: "how many
 * enemies are left in this map" is exactly the kind of thing that stops being a
 * game once it is known.
 */
namespace SarkoEncounter
{
	/** One encounter's server-side state for one raid. */
	struct FEncounterRuntime
	{
		/** Has already spent its budget once. With oneShot, that is forever. */
		bool bFired = false;

		/** The player is inside the radius and the encounter is trying to spawn. */
		bool bArmed = false;

		/**
		 * The hysteresis latch: true once the player has been further than
		 * armAfterUU since the last time this encounter armed. Starts true so a
		 * raid that begins outside every trigger can arm the first one it walks
		 * into.
		 */
		bool bBeyondRearm = true;

		/** How long the current arming attempt has been unable to find a usable point. */
		float DeferredSeconds = 0.f;

		/** Pawns this encounter created, for the maxAlive count. Weak: a corpse expires. */
		TArray<TWeakObjectPtr<AActor>> Spawned;
	};

	/**
	 * Pure: the hysteresis latch. Once the player is further than ArmAfterUU the
	 * encounter may arm again; nothing closer re-opens it. Without this a player
	 * standing on the boundary flips in and out of the radius several times a
	 * second and pumps the arming path.
	 */
	bool UpdateBeyondRearm(bool bWasBeyond, float DistanceUU, float ArmAfterUU);

	/**
	 * Pure: may this encounter arm right now? Fired-and-one-shot never arms
	 * again; nothing arms until the player is inside the radius AND the
	 * hysteresis latch is open.
	 */
	bool ShouldArm(bool bFired, bool bOneShot, bool bBeyondRearm, float DistanceUU, float RadiusUU);

	/**
	 * Pure: how many enemies this encounter may put on the map this instant.
	 *
	 * THE BUDGET IS THE LAW, and the first line is where that is enforced: an
	 * encounter whose cost does not fit in what is left of the raid budget
	 * spawns NOTHING. Not a partial wave — a two-bot warehouse fight with one
	 * bot is not the event that was authored, and half an event is worse than
	 * none in a raid that contains four enemies in total.
	 *
	 * bFirstFight is measured on the raid, not on the encounter: whichever
	 * encounter fires first is capped at FirstFightMaxAlive whatever its own
	 * maxAlive says. That is the "the first fight is one bot" rule, and it is
	 * enforced by the system rather than trusted to the authoring.
	 */
	int32 AllowedSpawnCount(
		int32 BudgetRemaining,
		int32 BudgetCost,
		int32 MaxAlive,
		int32 AliveNow,
		int32 AuthoredPoints,
		bool bFirstFight,
		int32 FirstFightMaxAlive);

	/**
	 * Pure: is this authored spawn point usable this instant?
	 *
	 * Two conditions, and they are AND rather than OR on purpose. The distance
	 * floor alone lets a bot appear as a visible dot on flat open ground 1800 uu
	 * straight up-screen; the sight check alone lets one appear 400 uu behind a
	 * crate the player is standing next to.
	 */
	bool SpawnPointQualifies(float DistanceToPlayerUU, bool bSpawnPointSeesPlayer, float MinDistanceUU);

	/**
	 * Pure: has an arming attempt waited long enough? The system never relocates
	 * a spawn toward the player and never spawns in view, so waiting is the only
	 * move it has — but a wait with no end is an encounter that is silently
	 * armed for the rest of the raid, so an attempt is abandoned after this and
	 * the trigger re-arms on the next approach.
	 */
	bool ShouldAbandonDeferral(float DeferredSeconds, float MaxDeferSeconds);

	/**
	 * Pure: which encounters this raid may activate, and in what priority order.
	 *
	 * THE TUTORIAL CASE is not a shuffle at all. It returns the non-optional rows
	 * sorted by their authored `order`, and nothing else — one enemy, then one,
	 * then two, in that sequence, every time. A first raid that taught its
	 * lessons in a different sequence each time would not be teaching.
	 *
	 * THE NORMAL CASE takes every row and shuffles it against the raid Seed. It
	 * is a priority order, not a script: an encounter still fires only when the
	 * player walks into its trigger. What the order decides is who gets the
	 * BUDGET when several POIs are reachable and the budget is scarcer than the
	 * pool — which on this map it is, deliberately: eight rows costing ten
	 * against a budget of eight. So four or five of the eight happen, and which
	 * four or five is a function of the seed. Same enemy count, same caps, same
	 * first-fight-1v1 law (AllowedSpawnCount enforces that on whichever row wins,
	 * so it survives the shuffle by construction), different geography.
	 *
	 * Fisher-Yates over an FRandomStream, which is the same deterministic
	 * generator the loot rolls use. Two properties matter and both are tested:
	 * the same seed gives the same order on every machine and every run (a raid
	 * is reproducible from `?Seed=`, and a "reproduction" that reproduces a
	 * different set of fights is worthless), and different seeds give different
	 * orders often enough to be the variation this exists to be.
	 *
	 * Seed is the RAID seed — replicated, and that is fine: knowing the order
	 * tells a client nothing it can act on, because the order is not the
	 * geography. Which POIs are where has always been in the map file.
	 */
	TArray<int32> BuildActivationOrder(
		const TArray<int32>& AuthoredOrders,
		const TArray<bool>& Optional,
		bool bTutorial,
		int32 Seed);
}
