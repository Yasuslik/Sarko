#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "SarkoShelterGameMode.generated.h"

/**
 * The main menu, as a game mode.
 *
 * No pawn, no HUD, no map: /Engine/Maps/Entry is an empty level and the whole
 * screen is a Slate widget the player controller puts in the viewport. It exists
 * as a game mode rather than as a widget on the raid game mode because the two
 * must not share a world — a shelter that ran alongside ASarkoRaidGameMode would
 * inherit its InitGame (which loads bridge.json and spawns 42 containers) and its
 * StartPlay (which opens a backend raid session).
 */
UCLASS()
class ASarkoShelterGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASarkoShelterGameMode();
};
