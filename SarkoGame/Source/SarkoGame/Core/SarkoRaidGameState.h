#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Map/SarkoMapBuilder.h"

#include "SarkoRaidGameState.generated.h"

// Forward-declared at file (global) scope on purpose, not inline as
// "struct FSarkoMapDefinition" inside a namespace: doing that in
// SarkoMapBuilder.h once already created a second, permanently-incomplete
// SarkoMap::FSarkoMapDefinition that shadowed the real ::FSarkoMapDefinition
// for every unqualified lookup inside that namespace. A class scope does not
// have that failure mode, but declaring it here — matching
// SarkoMapBuilder.h's fix — keeps one convention rather than two.
struct FSarkoMapDefinition;
class ASarkoLootContainer;
class ASarkoPropField;

/** How a raid ended. Replicated to everyone: the HUD's final screen reads it. */
UENUM()
enum class ESarkoRaidOutcome : uint8
{
	InProgress,
	/** Stood the full dwell in a zone. The backpack is the haul. */
	Extracted,
	/** HP hit zero. The backpack is already empty by the time this is set. */
	Died,
	/**
	 * The raid clock ran out. Spec §4.5: MIA is death — loot lost, submitted to
	 * the backend as `died`. It is the ceiling on a raid, not its goal.
	 */
	MIA
};

namespace SarkoRaid
{
	/**
	 * Whether a requested outcome may replace the current one. Pure, so the one
	 * rule that keeps a raid from being decided twice is unit tested with no
	 * world, no game mode and no network.
	 *
	 * First real outcome wins, and nothing may finish a raid into InProgress.
	 * The clock reaching zero on the same frame a dwell completes must not turn
	 * an extraction into an MIA, and a bullet landing on the extraction frame
	 * must not turn it into a KIA — the outcome is submitted to the backend once
	 * (Task 8), and the backend's idempotency is a safety net, not a licence.
	 */
	bool CanFinishRaid(ESarkoRaidOutcome Current, ESarkoRaidOutcome Requested);

	/**
	 * Whether an outcome costs the player everything they were carrying. Pure, so
	 * the other half of spec §4.5 — "MIA is death, loot lost" — is unit tested
	 * without a world.
	 *
	 * Extracted is the only outcome that keeps a haul. Died clears the backpack on
	 * the pawn's own death path, but MIA has no death path at all, so before this
	 * existed the clock running out left the haul intact and the MIA summary
	 * itemised loot the player had just lost. FinishRaid consults this before it
	 * writes Outcome, which is what makes the HUD's "the server emptied the
	 * backpack before the outcome was set" a fact rather than a hope.
	 */
	bool OutcomeLosesHaul(ESarkoRaidOutcome Outcome);

	/**
	 * Whether a raid may still be made live. Pure, so the guard is unit tested
	 * with no world and no HTTP.
	 *
	 * Two refusals, not one. `bSessionReady` catches the ordinary double
	 * activation (the offline fallback firing after a confirm already landed).
	 * A settled outcome catches the nastier one: the damage gate opens on
	 * IsRaidFinished() alone, so a player *can* be killed during the
	 * auth→start→confirm round trip, and the completion landing afterwards would
	 * otherwise hand a corpse a fresh seed and a fresh full clock under its own
	 * KIA summary — re-rolling every container against a seed the result was
	 * never submitted with.
	 */
	bool CanActivateRaid(bool bSessionReady, ESarkoRaidOutcome Outcome);

	/**
	 * Whether this machine should build and spawn its copy of the map now. Pure.
	 *
	 * Keyed on bSessionReady rather than on the seed. The seed used to be the
	 * trigger, and that was a bug: replication sends no change when a property
	 * equals its default, so an authoritative seed of exactly 0 never fired
	 * OnRep_Seed and left a joining client in an empty world with nothing logging
	 * it. bSessionReady is false until the raid is live and its flip always
	 * replicates, and "the raid has begun" is what the trigger actually means.
	 */
	bool ShouldSpawnClientLayout(bool bLayoutBuilt, bool bSessionReady);
}

/**
 * Raid clock and raid seed. The server owns both; every client reads
 * RemainingSeconds to draw the timer.
 *
 * The map itself never crosses the network, and does not need to: it is a data
 * file (Data/Maps/<MapId>.json) that ships with the build, so every machine
 * already has the geometry and can spawn its own identical copy by reading the
 * same file through the same pure ToLayout. Replicating hundreds of static
 * scenery actors instead would make the server pay to simulate props it does
 * not need.
 *
 * Seed replicates for a different reason: it is the shared basis for
 * server-authoritative rolls (loot contents). It contributes nothing to what
 * the geometry looks like, and it is not what tells a client to spawn it either
 * — `bSessionReady` is (OnRep_SessionReady), because a seed of exactly 0 equals
 * its own default and therefore replicates no change at all.
 */
UCLASS()
class ASarkoRaidGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ASarkoRaidGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Raid")
	float RemainingSeconds = 0.f;

	/**
	 * Shared basis for server-authoritative rolls. Set by the game mode on the
	 * server, which takes it from sarko-api's raid/start response. It does not
	 * shape the map — geometry comes from the map file — so changing it changes
	 * what is in the crates, not where they are.
	 *
	 * It is no longer the layout trigger. It used to be, and a seed of exactly 0
	 * equals this default, so replication sent no change and OnRep_Seed never
	 * fired: a joining client would have spawned no geometry at all.
	 * OnRep_SessionReady is the trigger now (SarkoRaid::ShouldSpawnClientLayout);
	 * OnRep_Seed still calls the same idempotent builder, because the seed
	 * arriving is also a perfectly good moment to have the map.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Seed, BlueprintReadOnly, Category = "Raid")
	int32 Seed = 0;

	/** Fires on clients when Seed replicates. No longer the layout trigger (see Seed), but still a fine moment to have the map: the builder is idempotent. */
	UFUNCTION()
	void OnRep_Seed();

	/**
	 * Loads this machine's map definition from disk and spawns its geometry
	 * into this machine's world. Idempotent — a second call is a no-op — because the server must
	 * call this explicitly right after setting Seed (a server never receives
	 * its own OnRep notify), while clients reach it through OnRep_SessionReady
	 * *and* OnRep_Seed, either of which may land first; without the guard a
	 * repeat OnRep (or simply both of them) would spawn a duplicate floor and
	 * cover set.
	 *
	 * "Either of which may land first" is not a free property of replication — it
	 * holds only because ASarkoRaidGameMode::ActivateRaid writes Seed and
	 * bSessionReady in the *same frame*. They therefore travel in one bunch, and
	 * the engine applies every property in a bunch before it calls any of that
	 * bunch's RepNotifies, so whichever notify runs first already sees
	 * bSessionReady == true and passes SarkoRaid::ShouldSpawnClientLayout.
	 *
	 * Split those two writes across frames and Seed arrives alone: on a client
	 * OnRep_Seed's build is refused by that gate and becomes a silent no-op, so
	 * the map's arrival depends entirely on bSessionReady turning up afterwards —
	 * and if anything ever keeps it from replicating, the client sits in an empty
	 * world with a perfectly good seed and nothing in any log to explain it. Keep
	 * the two writes together, or make OnRep_Seed's gate say so.
	 */
	void BuildAndSpawnLayout();

	/**
	 * Spawns this machine's geometry and props for a layout/definition that
	 * was already computed elsewhere (the game mode's InitGame loads the map
	 * definition — pure, no world needed — before this game state is
	 * guaranteed to exist, so it cannot spawn anything yet; StartPlay hands
	 * the result here once a world and this game state both exist). Guarded
	 * by the same bLayoutBuilt flag as BuildAndSpawnLayout, which calls this
	 * internally, so the two paths can never double-spawn between them.
	 *
	 * Off the authority it is additionally gated by
	 * SarkoRaid::ShouldSpawnClientLayout, so a client builds only once the raid is
	 * live. The server bypasses that gate: it reaches here from StartPlay, before
	 * it has set the very flag the gate reads.
	 */
	void SpawnPrebuiltLayout(const FSarkoMapLayout& InLayout, const FSarkoMapDefinition& InDefinition);

	/** The layout this machine read from the map file. Valid once BuildAndSpawnLayout or SpawnPrebuiltLayout has run. */
	const FSarkoMapLayout& GetLayout() const { return Layout; }

	/** Server only: begins the countdown. */
	void StartRaidClock(float DurationSeconds);

	bool IsRaidOver() const { return bClockStarted && RemainingSeconds <= 0.f; }

	/**
	 * How this raid ended, or InProgress. Written only by
	 * ASarkoRaidGameMode::FinishRaid, on the server, exactly once; replicated to
	 * everyone because it is what freezes input and draws the summary.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Raid")
	ESarkoRaidOutcome Outcome = ESarkoRaidOutcome::InProgress;

	bool IsRaidFinished() const { return Outcome != ESarkoRaidOutcome::InProgress; }

	/**
	 * True once ASarkoRaidGameMode::ReturnToShelter has actually scheduled the trip
	 * back — not merely once the raid ended.
	 *
	 * It exists because the two are not the same thing: ReturnToShelter returns
	 * early, having scheduled nothing, when there is no USarkoGameInstance to
	 * record the outcome on. The HUD's "ПОВЕРНЕННЯ ДО УКРИТТЯ..." line is a promise
	 * about the next few seconds, so it reads this rather than the outcome — a
	 * frozen dimmed world that promises a return it will never make is worse than
	 * one that admits it is stuck. Replicated because the HUD is drawn on each
	 * client and the decision is the server's; it lands in the same bunch as
	 * Outcome, since FinishRaid writes both in one frame.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Raid")
	bool bReturningToShelter = false;

	/**
	 * True once the raid's authoritative seed is in place — either because
	 * sarko-api opened a session, or because the backend is disabled or
	 * unreachable and the local seed is being used instead.
	 *
	 * Containers refuse to open until it is set, because a container looted
	 * against the placeholder seed and then re-derived against the real one would
	 * give two different hauls for one crate. The clock does not run either
	 * (ASarkoRaidGameMode::ActivateRaid starts it), so the round trip cannot cost
	 * the player raid time and cannot expire a raid into MIA before it began.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_SessionReady, BlueprintReadOnly, Category = "Raid")
	bool bSessionReady = false;

	/**
	 * Fires on clients the moment the raid goes live — the honest "the raid has
	 * begun" signal, and therefore where a client spawns its copy of the map.
	 * Replaces OnRep_Seed as the trigger; OnRep_Seed still calls the same
	 * idempotent builder, so whichever of the two properties a bunch delivers
	 * first, the layout is built exactly once.
	 */
	UFUNCTION()
	void OnRep_SessionReady();

	/**
	 * Whether the raid is live: the seed has landed and no outcome has been
	 * settled. Everything that turns time or position into value — opening a
	 * container, the extraction dwell — asks this rather than IsRaidFinished()
	 * alone, so neither end of the raid can be played through.
	 */
	bool IsLootable() const { return bSessionReady && !IsRaidFinished(); }

	/**
	 * One byte per container index: 1 means emptied. This is the only loot state
	 * on the wire.
	 *
	 * Spec §4.3 asks for a replicated `bLooted` on the container. A container is
	 * spawned locally on every machine from the map file — like every cover
	 * block — so it has no net identity and cannot replicate a property of its
	 * own; this array is the same fact, owned by the one actor that does
	 * replicate. A byte array rather than a bitmask because 42 bytes is nothing
	 * and a bitmask is a debugging tax.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_LootedContainers)
	TArray<uint8> LootedContainers;

	UFUNCTION()
	void OnRep_LootedContainers();

	/** Server only: sizes the array to the map's container count. Idempotent. */
	void SizeLootState(int32 ContainerCount);

	/** False for an out-of-range index — a client-supplied index is never trusted to be in range. */
	bool IsContainerLooted(int32 ContainerIndex) const;

	/** Server only. Bounds-checked; logs and does nothing for a bad index. */
	void MarkContainerLooted(int32 ContainerIndex);

	/** Containers register at BeginPlay so a replicated change can recolour them without anything ticking. */
	void RegisterContainer(ASarkoLootContainer* Container);

	/**
	 * The sector's instanced props register here at BeginPlay, exactly as the
	 * containers do and for exactly the same reason: Tick has to drive the canopy
	 * fade every frame and must not run a TActorIterator to find who to drive.
	 *
	 * There is one field per machine. A second registration replaces the first
	 * rather than accumulating, because the only way to get one is to spawn a
	 * second map, and then the first field is the stale one.
	 */
	void RegisterPropField(ASarkoPropField* Field);

	/** Whatever registered last, or null before the layout has spawned. */
	ASarkoPropField* GetPropField() const;

	/**
	 * Every container that has spawned on this machine, in no particular order —
	 * registration appends, but OnRep_LootedContainers drops stale entries with
	 * RemoveAtSwap, so any removal reshuffles the tail. Nothing indexes into this
	 * (the looted state is keyed by the map file's container index, not by a
	 * position here), so an unspecified order costs nothing and a swap-remove is
	 * cheaper than preserving one.
	 *
	 * This registry is the machine's container list, so nothing else needs to run
	 * a TActorIterator to find them — and in particular the player controller's
	 * per-frame proximity check does not (an iterator heap-allocates, and it had no
	 * way to tell "the containers have not spawned yet" from "this map has none",
	 * so on a container-less map it rescanned the whole world every frame forever).
	 * Late-spawning is handled for free: a container added after the first
	 * PlayerTick — which is the normal case on a client, where containers spawn
	 * from OnRep_Seed — appears here the moment it registers.
	 *
	 * Weak entries can be stale; callers must null-check. Stale ones are dropped
	 * by OnRep_LootedContainers rather than eagerly.
	 */
	const TArray<TWeakObjectPtr<ASarkoLootContainer>>& GetContainers() const { return RegisteredContainers; }

private:
	/**
	 * Hides the canopies over the LOCAL player's head, every frame, and does
	 * nothing anywhere else.
	 *
	 * The gate is IsLocalController(), not HasAuthority(), and the difference
	 * matters in both directions. A dedicated server has no local player
	 * controller at all, so this returns immediately and the server never spends
	 * a cycle on a thing nobody is looking at — and, more importantly, never
	 * changes anything a client could then disagree with. A listen server DOES
	 * have a local player, and its canopies must open over its own pawn exactly
	 * as a client's do: the effect belongs to a camera, not to an authority.
	 *
	 * Nothing here is replicated, and nothing here may ever become replicated.
	 * The fade is a fact about one machine's render data.
	 */
	void UpdateCanopyFade();

	bool bClockStarted = false;
	bool bLayoutBuilt = false;
	FSarkoMapLayout Layout;

	/** Weak: containers are destroyed with the world and this must not keep them alive. */
	TArray<TWeakObjectPtr<ASarkoLootContainer>> RegisteredContainers;

	/** Weak for the same reason the containers are: the world owns it, not this. */
	TWeakObjectPtr<ASarkoPropField> PropField;
};
