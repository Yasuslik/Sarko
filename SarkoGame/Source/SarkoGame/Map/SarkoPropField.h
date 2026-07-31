#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SarkoPropField.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
struct FSarkoPropPart;

/**
 * One canopy instance, and everything the fade needs to know about it.
 *
 * Not a UPROPERTY struct and not reflected: nothing here replicates, saves or
 * crosses a Blueprint boundary. The component is held weakly because the array
 * is walked every time the player moves and a torn-down level must not be kept
 * alive by it — the strong reference is ASarkoPropField's own component map.
 */
struct FSarkoCanopyInstance
{
	TWeakObjectPtr<UInstancedStaticMeshComponent> Component;

	/** Index into that component's instance array. Stable: nothing is ever removed. */
	int32 InstanceIndex = INDEX_NONE;

	/** World position of the canopy, cached so the distance test touches no UObject. */
	FVector Location = FVector::ZeroVector;

	/** The transform this canopy has when it is visible — restored on the way back. */
	FTransform Shown;

	bool bHidden = false;
};

/**
 * Every prop part in the sector, as instances rather than as actors.
 *
 * WHY THIS EXISTS AT ALL. A forest is hundreds of trees, and until now one
 * authored prop part was one AStaticMeshActor: 401 of them before a single tree
 * was placed, against a ceiling of 420. Adding a real forest the honest way
 * would have meant roughly nine hundred actors, and ТЗ §16's budget is not
 * advice. So the props migrate to instancing, which was blocked until Stage B
 * replaced the per-actor UMaterialInstanceDynamic with one shared instance per
 * surface — UE batches primitives that share a mesh AND a material, and a unique
 * material per actor is precisely what stopped it.
 *
 * ONE COMPONENT PER (mesh, surface, collision, canopy). The first two are what
 * a draw call is keyed on. Collision is in the key because a component carries
 * one collision setting for all its instances, so a bush and a rock cannot share
 * one even though nothing else separates them. The canopy flag is in the key for
 * a subtler reason: canopies are the only instances in the sector that ever
 * change, and keeping them out of the components that never do means the static
 * bulk of the map is never marked dirty for something happening to a tree.
 *
 * THE FADE. The camera looks almost straight down, so a canopy over the player's
 * head is between the camera and the player. UpdateCanopyFade shrinks every
 * canopy within a radius of the local pawn to nothing and restores it on the way
 * out. It is cosmetic, local and client-only in the only sense that matters: it
 * touches an instance transform on this machine's render data and NOTHING else.
 * No collision setting, no gameplay state, no replication — a hidden canopy
 * stops exactly as many bullets as a visible one, which is none.
 *
 * Everything here is game thread only, like the rest of Map/.
 */
UCLASS()
class ASarkoPropField : public AActor
{
	GENERATED_BODY()

public:
	ASarkoPropField();

	virtual void BeginPlay() override;

	/**
	 * Adds one prop part at a world transform, creating the component for its
	 * (mesh, surface, collision, canopy) key on first use. A canopy part is also
	 * recorded for the fade.
	 *
	 * The scale convention is SpawnMeshBox's, unchanged: every engine primitive
	 * is 100 uu across its bounding box, so the instance scale is extent / 50.
	 */
	void AddPart(UStaticMesh* Mesh, const FSarkoPropPart& Part, const FVector& WorldLocation,
		const FRotator& WorldRotation);

	/**
	 * Hides every canopy within RadiusUU of ViewerLocation (planar distance —
	 * the camera is overhead, so the pawn's own height is irrelevant) and shows
	 * every canopy outside it.
	 *
	 * Cheap by construction and it has to be, because it is reached from Tick:
	 *
	 *  - it walks ONE pre-built array of plain structs, never the scene, and
	 *    never allocates;
	 *  - it returns immediately unless the viewer has actually moved a
	 *    meaningful distance since the last pass, so a standing player costs a
	 *    single squared-distance compare per frame and nothing else;
	 *  - it touches the engine only for canopies whose state actually CHANGED,
	 *    which at walking pace is a handful per second out of a few hundred.
	 *
	 * Safe to call with no canopies, on a machine with no local player, or twice
	 * in a frame.
	 */
	void UpdateCanopyFade(const FVector& ViewerLocation, float RadiusUU);

	/**
	 * How many instanced components this field ended up with — the number that
	 * replaced "how many actors" as the thing the budget bounds, since it is what
	 * the renderer pays per frame. Pinned by
	 * Sarko.Map.PropInstanceCountIsWithinTheMobileBudget against the pure
	 * SarkoMap::CountInstancedComponents, which predicts it from the map file.
	 */
	int32 GetInstancedComponentCount() const { return ComponentsByKey.Num(); }

	/** Total instances across every component — one per authored prop part. */
	int32 GetInstanceCount() const;

	/** How many of those are canopies, i.e. how long the fade's array is. */
	int32 GetCanopyCount() const { return Canopies.Num(); }

private:
	UInstancedStaticMeshComponent* FindOrCreateComponent(UStaticMesh* Mesh, const FSarkoPropPart& Part);

	/**
	 * Keyed by a string built from the mesh path, surface, collision and canopy
	 * flags. A string rather than a packed integer because this map is touched
	 * once per prop part at level load and never again, and a key you can read in
	 * a debugger is worth more here than the allocations it costs.
	 */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> ComponentsByKey;

	TArray<FSarkoCanopyInstance> Canopies;

	/** Where the viewer was when the fade last ran. Meaningless until bFadeHasRun. */
	FVector LastFadeLocation = FVector::ZeroVector;

	float LastFadeRadiusUU = 0.f;

	bool bFadeHasRun = false;
};
