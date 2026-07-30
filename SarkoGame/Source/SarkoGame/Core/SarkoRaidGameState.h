#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Map/SarkoMapBuilder.h"

#include "SarkoRaidGameState.generated.h"

/**
 * Raid clock and map seed. The server owns both; every client reads
 * RemainingSeconds to draw the timer.
 *
 * The map itself never crosses the network: replicating forty-plus static
 * cover actors would be the wrong trade against a four-byte seed, and it
 * would make the server pay to simulate scenery it does not need. Instead
 * only Seed replicates, and every machine — server and client alike —
 * generates the identical layout locally by calling BuildLayout then
 * SpawnLayout from that value, which is exactly what BuildLayout's
 * determinism exists for. See OnRep_Seed and BuildAndSpawnLayout.
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

	/** Seed for the procedural map layout. Set by the game mode on the server. */
	UPROPERTY(ReplicatedUsing = OnRep_Seed, BlueprintReadOnly, Category = "Raid")
	int32 Seed = 0;

	/** Fires on clients the moment Seed replicates; builds and spawns this machine's copy of the map. */
	UFUNCTION()
	void OnRep_Seed();

	/**
	 * Builds the layout from Seed and spawns its geometry into this machine's
	 * world. Idempotent — a second call is a no-op — because the server must
	 * call this explicitly right after setting Seed (a server never receives
	 * its own OnRep notify), while clients reach it only through OnRep_Seed;
	 * without the guard a repeat OnRep (e.g. after a seamless travel edge
	 * case) would spawn a duplicate floor and cover set.
	 */
	void BuildAndSpawnLayout();

	/** The layout this machine generated from Seed. Valid once BuildAndSpawnLayout has run. */
	const FSarkoMapLayout& GetLayout() const { return Layout; }

	/** Server only: begins the countdown. */
	void StartRaidClock(float DurationSeconds);

	bool IsRaidOver() const { return bClockStarted && RemainingSeconds <= 0.f; }

private:
	bool bClockStarted = false;
	bool bLayoutBuilt = false;
	FSarkoMapLayout Layout;
};
