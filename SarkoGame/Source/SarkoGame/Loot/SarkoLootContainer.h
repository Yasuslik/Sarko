#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SarkoLootContainer.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

namespace SarkoLoot
{
	/**
	 * Whether this pawn may open this container, right now.
	 *
	 * Pure and shared: the client calls it to decide whether to draw a prompt,
	 * and the server calls it — with its own copy of the pawn's location — to
	 * decide whether to honour the RPC. Both must agree on the rule, and only
	 * the server's answer counts.
	 *
	 * Distance is planar. The container sits on the ground while the pawn's
	 * origin is at capsule centre, so a 3D check would make a crate at the
	 * player's feet unreachable at the edge of the radius, which reads as the
	 * prompt flickering for no reason.
	 */
	bool CanInteract(const FVector& PawnLocation, const FVector& ContainerLocation,
		float RadiusUU, bool bPawnAlive, bool bAlreadyLooted);
}

/**
 * One lootable container.
 *
 * **Not a replicated actor.** Every machine spawns its own identical set from
 * Data/Maps/bridge.json, exactly as it does for cover blocks: the map never
 * crosses the network (see ASarkoRaidGameState's comment), and a locally-spawned
 * actor has no net identity to replicate a property through even if it wanted
 * one. The single mutable fact — which indices have been emptied — lives on the
 * game state as ASarkoRaidGameState::LootedContainers and replicates from there.
 *
 * ContainerIndex is the index into FSarkoMapDefinition::Containers. It is the
 * name of this container on the wire, one of the three inputs to its loot roll's
 * stream seed (the others being the raid seed and the server-only
 * ASarkoRaidGameMode::LootSalt — index and seed alone are both client-knowable,
 * which is exactly why the salt exists), and the only thing a client sends when
 * it asks to open something — so every use of it is bounds-checked.
 */
UCLASS()
class ASarkoLootContainer : public AActor
{
	GENERATED_BODY()

public:
	ASarkoLootContainer();

	virtual void BeginPlay() override;

	/** Index into the map definition's Containers array. Set at spawn, never changed. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int32 ContainerIndex = INDEX_NONE;

	/** Loot table tier, copied from the map definition. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	FName Tier;

	/** Reads the replicated bit off the game state — never a local guess. */
	bool IsLooted() const;

	/** Recolours the lid to match IsLooted(). Called at spawn and whenever the replicated state changes. */
	void RefreshVisualState();

	void SetupFromSpot(int32 InIndex, FName InTier);

private:
	/**
	 * A bare scene root, with the crate's two boxes as siblings under it.
	 *
	 * Not the body mesh itself: a child's *relative* location is expressed in
	 * its parent's scaled space, and the body is scaled non-uniformly to make a
	 * crate out of the engine's 100 uu cube, so a lid attached to it would sit
	 * at 0.64 of the offset it was given and sink into the body. Siblings under
	 * an unscaled root keep their offsets in unreal units.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> Lid;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LidMaterial;
};
