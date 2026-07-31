#include "Shelter/SarkoShelterGameMode.h"

#include "Shelter/SarkoShelterPlayerController.h"

ASarkoShelterGameMode::ASarkoShelterGameMode()
{
	// Nothing ticks and nothing spawns. The default AGameModeBase behaviour is
	// already this; every line below is an explicit refusal of something the raid
	// game mode does, so a future edit has to argue with a name rather than a
	// silence.
	PrimaryActorTick.bCanEverTick = false;

	PlayerControllerClass = ASarkoShelterPlayerController::StaticClass();

	// No pawn: there is nothing to walk around in. bStartPlayersAsSpectators
	// keeps RestartPlayer from running at all, so a null DefaultPawnClass cannot
	// produce the "failed to spawn pawn" warning on every boot.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;

	// No AHUD. The menu is Slate in the viewport, and an AHUD here would draw
	// underneath it for no reason.
	HUDClass = nullptr;
}
