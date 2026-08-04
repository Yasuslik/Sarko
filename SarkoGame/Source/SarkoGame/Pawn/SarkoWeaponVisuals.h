#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"

class ACharacter;

/**
 * The weapon a pawn is SEEN to be carrying.
 *
 * Nothing here knows what a weapon does. Damage, magazine size and range are
 * USarkoWeaponComponent's and the bot archetype table's business and are
 * deliberately absent: this module answers one question, "what should be in
 * this character's hand", and it answers it identically on every machine.
 *
 * The meshes are the three from Quaternius' Ultimate Gun Pack that survived the
 * theme pass (Content/ThirdParty/LICENSES.md records the ones that did not).
 * They come through the same Scripts/import-assets.sh as the props, but in the
 * pipeline's `weapon` mode: scaled uniformly to the real gun's real length in
 * centimetres and stood on Z=0, NOT stretched into the -50..50 box a prop kind's
 * `Extent / 50` assumes. A weapon must therefore never be given a prop kind —
 * Sarko.Config.PropMeshBoundsAreNormalised walks the kind table and would fail
 * on one, which is the alarm working rather than a defect.
 */
namespace SarkoWeaponVisuals
{
	/**
	 * The held mesh for an item id, or nullptr when the item is not a weapon
	 * with a mesh. An unknown id is not an error: `pistol` is the only weapon in
	 * the shipped catalog that a stash can hold today, and an unarmed pawn is a
	 * legal state the raid start path already logs.
	 */
	const TCHAR* MeshPathForItem(FName ItemId);

	/** Every item id that has a held mesh. The test walks this rather than a second list. */
	TArray<FName> ArmedItemIds();

	/**
	 * The bone or socket the weapon hangs from — Manny's right hand.
	 *
	 * A BONE, not a socket: SKM_Manny_Simple ships no weapon socket of any kind
	 * (nothing in this project ever added one, and Epic's simple mannequin has
	 * none authored), so the attachment names the `hand_r` bone directly and
	 * carries its own relative transform. USkeletalMeshComponent::DoesSocketExist
	 * answers true for a bone name, and AttachToComponent resolves one the same
	 * way, so the code path is identical; only the fitting is ours.
	 */
	FName HandBoneName();

	/**
	 * Where the grip is, in the gun's own frame — the only authored part of the
	 * fitting.
	 *
	 * The import convention puts a weapon's origin on the ground under the
	 * middle of the gun, and a grip is neither of those things, so this says how
	 * far forward and down the mesh has to move for the grip to land in the
	 * fist. Three centimetre measurements per weapon, in the same spirit as the
	 * prop kind table's extents.
	 *
	 * There is deliberately NO authored rotation to go with it: the alignment is
	 * derived from the mannequin's own reference skeleton inside SetHeldWeapon,
	 * because guessing Euler angles against somebody else's rig produced a rifle
	 * aimed straight down the camera and therefore invisible.
	 */
	FVector GripOffsetFor(FName ItemId);

	/**
	 * Puts ItemId's mesh in the character's right hand, replacing whatever was
	 * there. NAME_None — or any id with no mesh — leaves the pawn empty-handed
	 * and destroys the component, so "unarmed" is a state that costs nothing.
	 *
	 * Cosmetic on every machine and safe to call on all of them: the component
	 * is created locally, never replicated, and never collides. What replicates
	 * is the item id on ASarkoCharacter, which is the one fact clients must
	 * agree on.
	 */
	void SetHeldWeapon(ACharacter& Character, FName ItemId);
}
