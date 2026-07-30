#pragma once

#include "CoreMinimal.h"

class ACharacter;

namespace SarkoBody
{
	/**
	 * Gives a character something visible to look at.
	 *
	 * ACharacter ships with an empty skeletal mesh component, so a pawn with no
	 * mesh assigned renders as nothing at all — the player cannot see their own
	 * character, and enemies are invisible while still shooting. No headless
	 * test can catch that, because -nullrhi renders nothing to look at either.
	 *
	 * This project authors no assets, so the body is an engine primitive
	 * referenced by path, tinted through a dynamic material instance created at
	 * runtime. Purely cosmetic: the capsule still owns collision and movement,
	 * and this mesh has collision disabled so it cannot interfere with a trace.
	 *
	 * @param Character  the pawn to give a body to
	 * @param Tint       body colour, so the player can tell themselves from an enemy
	 */
	void AttachPlaceholderBody(ACharacter& Character, const FLinearColor& Tint);
}
