#include "Core/SarkoPlayerController.h"

#include "Combat/SarkoWeapon.h"
#include "Pawn/SarkoCharacter.h"

bool SarkoInput::IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize)
{
	return ScreenPosition.X < ViewportSize.X * 0.5f;
}

ASarkoPlayerController::ASarkoPlayerController()
{
	bShowMouseCursor = false;
	// Touch is the only input path in this slice.
	DefaultMouseCursor = EMouseCursor::None;
}

void ASarkoPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdateSticks();

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		return;
	}

	float CameraYaw = 0.f;
	if (PlayerCameraManager)
	{
		CameraYaw = PlayerCameraManager->GetCameraRotation().Yaw;
	}

	// StickToWorldDirection is pure and world-free, but it normalises its
	// result — that is correct for aim (only direction matters there) and
	// wrong for movement, where SetMoveIntent reads the vector's *magnitude*
	// as the analog deflection (dead zone + speed scale, see Task 3). Scale
	// the world-space direction back up by the stick's own deflection so the
	// magnitude survives the rotation instead of being thrown away.
	bool bKeyboardMoved = false;
#if !UE_BUILD_SHIPPING
	bKeyboardMoved = ApplyDesktopTestInput(*Pawn, CameraYaw);
#endif

	if (!bKeyboardMoved)
	{
		const FVector2D MoveValue = MoveStick.Value();
		const FVector2D MoveDirection = SarkoAim::StickToWorldDirection(MoveValue, CameraYaw);
		Pawn->SetMoveIntent(MoveDirection * MoveValue.Size());
	}

	const FVector2D AimValue = AimStick.Value();
	Pawn->SetAimIntent(SarkoAim::StickToWorldDirection(AimValue, CameraYaw), AimStick.bActive);

	// Release of the aim thumb is the only fire signal — never auto-fire
	// (spec §9). bAimReleasedThisFrame is a one-frame edge, so this fires
	// exactly once per release.
	if (bAimReleasedThisFrame)
	{
		Pawn->RequestFire();
	}
}

void ASarkoPlayerController::CheatEmptyMagazine()
{
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("CheatEmptyMagazine: no possessed ASarkoCharacter to fire"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("CheatEmptyMagazine: before ammo=%d reloading=%d"),
		Pawn->WeaponComponent->GetAmmoInMagazine(), Pawn->WeaponComponent->IsReloading());

	// Deliberately more than a full magazine: the tail calls land after ammo
	// hits zero and must be absorbed by the CanFire() gate (auto-starting a
	// reload, never auto-firing) instead of crashing or silently firing.
	for (int32 Shot = 0; Shot < 35; ++Shot)
	{
		Pawn->RequestFire();
		UE_LOG(LogTemp, Log, TEXT("CheatEmptyMagazine: shot=%d ammo=%d reloading=%d"),
			Shot, Pawn->WeaponComponent->GetAmmoInMagazine(), Pawn->WeaponComponent->IsReloading());
	}

	UE_LOG(LogTemp, Log, TEXT("CheatEmptyMagazine: after ammo=%d reloading=%d"),
		Pawn->WeaponComponent->GetAmmoInMagazine(), Pawn->WeaponComponent->IsReloading());
}

void ASarkoPlayerController::UpdateSticks()
{
	bAimReleasedThisFrame = false;

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	const FVector2D Viewport(static_cast<float>(ViewportX), static_cast<float>(ViewportY));

	const bool bWasAiming = AimStick.bActive;

	bool bMoveTouchStillDown = false;
	bool bAimTouchStillDown = false;

	// Two fingers are enough for this scheme; poll both touch slots. Each
	// slot is a stable identity for one held finger (UE keeps a finger in the
	// same slot from press to release), so once a slot is bound to a stick it
	// keeps driving that stick regardless of which half its finger currently
	// sits over — classification happens once, at press time.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		float TouchX = 0.f;
		float TouchY = 0.f;
		bool bPressed = false;
		GetInputTouchState(static_cast<ETouchIndex::Type>(Index), TouchX, TouchY, bPressed);

		if (Index == MoveTouchIndex)
		{
			if (bPressed)
			{
				MoveStick.Current = FVector2D(TouchX, TouchY);
				bMoveTouchStillDown = true;
			}
			continue;
		}

		if (Index == AimTouchIndex)
		{
			if (bPressed)
			{
				AimStick.Current = FVector2D(TouchX, TouchY);
				bAimTouchStillDown = true;
			}
			continue;
		}

		if (!bPressed)
		{
			continue;
		}

		// A touch this controller has not seen before: it claims whichever
		// stick is currently free on its landing half. If that stick is
		// already claimed by another finger, this touch drives nothing —
		// only two sticks exist, so a third finger is simply ignored.
		const FVector2D Position(TouchX, TouchY);
		if (SarkoInput::IsLeftHalf(Position, Viewport))
		{
			if (MoveTouchIndex == INDEX_NONE)
			{
				MoveTouchIndex = Index;
				MoveStick.bActive = true;
				MoveStick.Origin = Position;
				MoveStick.Current = Position;
				bMoveTouchStillDown = true;
			}
		}
		else
		{
			if (AimTouchIndex == INDEX_NONE)
			{
				AimTouchIndex = Index;
				AimStick.bActive = true;
				AimStick.Origin = Position;
				AimStick.Current = Position;
				bAimTouchStillDown = true;
			}
		}
	}

	if (!bMoveTouchStillDown)
	{
		MoveStick = FSarkoTouchStick();
		MoveTouchIndex = INDEX_NONE;
	}
	if (!bAimTouchStillDown)
	{
		AimStick = FSarkoTouchStick();
		AimTouchIndex = INDEX_NONE;
	}

	// Release of the aim thumb is the fire signal — never auto-fire (spec §9).
	bAimReleasedThisFrame = bWasAiming && !AimStick.bActive;
}

#if !UE_BUILD_SHIPPING
bool ASarkoPlayerController::ApplyDesktopTestInput(ASarkoCharacter& Pawn, float CameraYaw)
{
	// WASD, read straight off the key state — no Enhanced Input action needed,
	// which matters because input actions are binary assets this project cannot
	// author.
	FVector2D Stick = FVector2D::ZeroVector;
	if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up))    { Stick.Y += 1.f; }
	if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down))  { Stick.Y -= 1.f; }
	if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) { Stick.X += 1.f; }
	if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left))  { Stick.X -= 1.f; }

	const bool bMoved = !Stick.IsNearlyZero();
	if (bMoved)
	{
		// Digital keys are full deflection; normalise so diagonals are not faster.
		Stick.Normalize();
		Pawn.SetMoveIntent(SarkoAim::StickToWorldDirection(Stick, CameraYaw));
	}

	// WasInputKeyJustPressed is a one-frame edge, which keeps the no-auto-fire
	// rule intact: holding space does not stream shots, exactly as holding the
	// aim thumb does not.
	if (WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		Pawn.RequestFire();
	}

	return bMoved;
}
#endif
