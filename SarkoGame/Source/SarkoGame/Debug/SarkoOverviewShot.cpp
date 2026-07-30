#include "Debug/SarkoOverviewShot.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Headroom beyond the sector edge, so outer geometry is not flush to the border. */
	constexpr float FrameMargin = 1.15f;

	/** Straight down. */
	const FRotator LookDown(-90.f, 0.f, 0.f);
}

float SarkoDebug::HeightToFitSector(float ExtentUU, float VerticalFOVDegrees)
{
	// Half the frame subtends half the FOV, so height = halfExtent / tan(FOV/2).
	const float HalfFOVRadians = FMath::DegreesToRadians(FMath::Clamp(VerticalFOVDegrees, 10.f, 170.f) * 0.5f);
	const float Height = ExtentUU / FMath::Max(FMath::Tan(HalfFOVRadians), KINDA_SMALL_NUMBER);
	return Height * FrameMargin;
}

void SarkoDebug::FrameWholeSector(APlayerController& Controller, float ExtentUU)
{
	UWorld* World = Controller.GetWorld();
	if (!World)
	{
		return;
	}

	float FOV = 90.f;
	if (const APlayerCameraManager* Camera = Controller.PlayerCameraManager)
	{
		FOV = Camera->GetFOVAngle();
	}

	const float Height = HeightToFitSector(ExtentUU, FOV);

	// A plain PlayerController-as-ViewTarget does not work here: every tick,
	// APlayerCameraManager::UpdateViewTarget falls through to
	// Target->CalcCamera(), and APlayerController::CalcCamera() returns
	// GetFocalLocation() — the *possessed pawn's* location — which stomps any
	// manual camera position back to ground level on the very next frame.
	// A spawned ACameraActor is handled as a special case one level up (its
	// UCameraComponent::GetCameraView is read directly), so its transform is
	// not fought by CalcCamera. That is why this spawns a temporary camera
	// instead of pushing a location onto the PlayerCameraManager.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* OverviewCamera = World->SpawnActor<ACameraActor>(FVector(0.f, 0.f, Height), LookDown, SpawnParams);
	if (!OverviewCamera)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoOverview: failed to spawn the overview camera"));
		return;
	}
	UCameraComponent* CameraComponent = OverviewCamera->GetCameraComponent();
	CameraComponent->SetFieldOfView(FOV);
	// The screenshot is square (1600x1600), but ACameraActor's component
	// defaults to a 16:9 AspectRatio; left alone, bConstrainAspectRatio
	// letterboxes the render inside the square canvas, wasting most of the
	// vertical FOV the height math above was computed for. Forcing it to 1:1
	// makes horizontal and vertical FOV equal, matching HeightToFitSector's
	// assumption that VerticalFOVDegrees is the FOV that actually governs
	// what fits top-to-bottom.
	CameraComponent->AspectRatio = 1.f;

	Controller.SetViewTargetWithBlend(OverviewCamera, 0.f);

	UE_LOG(LogTemp, Display, TEXT("SarkoOverview: framing %.0f uu sector from %.0f uu up at %.0f deg FOV"),
		ExtentUU, Height, FOV);
}
