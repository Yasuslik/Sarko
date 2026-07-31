#pragma once

#include "CoreMinimal.h"

class ACharacter;

namespace SarkoBody
{
	/**
	 * Which side this pawn is on, for the purposes of looking like it.
	 *
	 * Deliberately not ESarkoTeam: that enum is the *rule* the server applies to
	 * damage and aim assist, and it is not replicated (see its comment). This is
	 * a cosmetic choice each pawn class makes about itself, on every machine.
	 */
	enum class ESide : uint8
	{
		Player,
		Enemy
	};

	/**
	 * Gives a character a real, textured, animatable body.
	 *
	 * ACharacter ships with an *empty* skeletal mesh component, so a pawn with no
	 * mesh assigned renders as nothing at all — the player cannot see their own
	 * character, and enemies are invisible while still shooting. No headless test
	 * can catch that, because -nullrhi renders nothing to look at either; only a
	 * -RenderOffscreen screenshot can.
	 *
	 * This project authors no assets, so the body is Epic's first-party mannequin
	 * referenced by path — exactly as the previous placeholder referenced
	 * /Engine/BasicShapes — and never edited. Purely cosmetic: the capsule still
	 * owns collision and movement, and this mesh has collision disabled so it
	 * cannot intercept a shot that was aimed at the pawn.
	 *
	 * Animation is *not* set up here; USarkoCharacterAnimComponent owns that, so
	 * this function does one job and the two can be reasoned about separately.
	 *
	 * @param Character  the pawn to give a body to
	 * @param Side       which mesh and tint to use, so friend and foe differ
	 */
	void AttachCharacterMesh(ACharacter& Character, ESide Side);

	/** Skeletal mesh asset path for a side. Exposed so a headless test can prove it resolves. */
	const TCHAR* MeshPathForSide(ESide Side);
}
