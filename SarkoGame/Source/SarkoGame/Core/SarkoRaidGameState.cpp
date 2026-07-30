#include "Core/SarkoRaidGameState.h"

#include "Net/UnrealNetwork.h"

ASarkoRaidGameState::ASarkoRaidGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASarkoRaidGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASarkoRaidGameState, RemainingSeconds);
}

void ASarkoRaidGameState::StartRaidClock(float DurationSeconds)
{
	RemainingSeconds = DurationSeconds;
	bClockStarted = true;
}

void ASarkoRaidGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The clock only runs on the server; clients receive RemainingSeconds.
	if (!HasAuthority() || !bClockStarted)
	{
		return;
	}
	RemainingSeconds = FMath::Max(0.f, RemainingSeconds - DeltaSeconds);
}
