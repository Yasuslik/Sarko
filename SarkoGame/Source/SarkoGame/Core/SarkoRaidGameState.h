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
 * server-authoritative rolls (loot contents, in a later plan). Its arrival on
 * a client also happens to be the signal that the raid has begun, which is
 * what makes OnRep_Seed the moment a client spawns its geometry — but the seed
 * contributes nothing to what that geometry looks like.
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
	 * Shared basis for server-authoritative rolls (loot, in a later plan). Set
	 * by the game mode on the server, which takes it from sarko-api's
	 * raid/start response. It does not shape the map — geometry comes from the
	 * map file — so changing it changes what is in the crates, not where they
	 * are.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Seed, BlueprintReadOnly, Category = "Raid")
	int32 Seed = 0;

	/** Fires on clients the moment Seed replicates — the earliest point a client knows the raid has begun, and so where it spawns its copy of the map. */
	UFUNCTION()
	void OnRep_Seed();

	/**
	 * Loads this machine's map definition from disk and spawns its geometry
	 * into this machine's world. Idempotent — a second call is a no-op — because the server must
	 * call this explicitly right after setting Seed (a server never receives
	 * its own OnRep notify), while clients reach it only through OnRep_Seed;
	 * without the guard a repeat OnRep (e.g. after a seamless travel edge
	 * case) would spawn a duplicate floor and cover set.
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
	 */
	void SpawnPrebuiltLayout(const FSarkoMapLayout& InLayout, const FSarkoMapDefinition& InDefinition);

	/** The layout this machine read from the map file. Valid once BuildAndSpawnLayout or SpawnPrebuiltLayout has run. */
	const FSarkoMapLayout& GetLayout() const { return Layout; }

	/** Server only: begins the countdown. */
	void StartRaidClock(float DurationSeconds);

	bool IsRaidOver() const { return bClockStarted && RemainingSeconds <= 0.f; }

private:
	bool bClockStarted = false;
	bool bLayoutBuilt = false;
	FSarkoMapLayout Layout;
};
