#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"

#include "SarkoRaidGameState.generated.h"

/** Raid clock. The server owns it; every client reads it to draw the timer. */
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

	/** Server only: begins the countdown. */
	void StartRaidClock(float DurationSeconds);

	bool IsRaidOver() const { return bClockStarted && RemainingSeconds <= 0.f; }

private:
	bool bClockStarted = false;
};
