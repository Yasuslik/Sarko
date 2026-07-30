#include "Core/SarkoPlayerController.h"

#include "Combat/SarkoWeapon.h"
#include "Core/SarkoRaidSettings.h"
#include "Debug/SarkoOverviewShot.h"
#include "EngineUtils.h"
#include "Loot/SarkoLootContainer.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"

bool SarkoInput::IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize)
{
	return ScreenPosition.X < ViewportSize.X * 0.5f;
}

FBox2D SarkoInput::InteractButtonRect(FVector2D ViewportSize)
{
	// Scaled to the shorter axis so the button is the same physical size on a
	// phone and in a window, with a floor so it never drops below a thumb.
	const float Size = FMath::Max(96.f, FMath::Min(ViewportSize.X, ViewportSize.Y) * 0.14f);
	const float Right = ViewportSize.X - Size - Size * 0.35f;
	const float Top = ViewportSize.Y * 0.5f - Size * 0.5f;
	return FBox2D(FVector2D(Right, Top), FVector2D(Right + Size, Top + Size));
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
	UpdateInteract();

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

	const FBox2D InteractRect = SarkoInput::InteractButtonRect(Viewport);
	bool bInteractTouchStillDown = false;

	// Three fingers now, not two: movement, aim and the interact button are
	// three separate holds, and a player opening a crate while backing away
	// from a bot is holding all three at once. Each slot is a stable identity
	// for one held finger (UE keeps a finger in the same slot from press to
	// release), so once a slot is bound to a stick — or to the button — it keeps
	// driving that thing regardless of where its finger currently sits;
	// classification happens once, at press time.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		float TouchX = 0.f;
		float TouchY = 0.f;
		bool bPressed = false;
		GetInputTouchState(static_cast<ETouchIndex::Type>(Index), TouchX, TouchY, bPressed);

		if (Index == InteractTouchIndex)
		{
			if (bPressed)
			{
				bInteractTouchStillDown = true;
			}
			continue;
		}

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

		// A touch this controller has not seen before: it claims the interact
		// button if it landed on it, otherwise whichever stick is currently free
		// on its landing half. If that thing is already claimed by another
		// finger, this touch drives nothing — there are only three consumers, so
		// a fourth finger is simply ignored.
		const FVector2D Position(TouchX, TouchY);

		// The interact button wins over the aim stick for a touch that lands on
		// it. Without this, pressing it would also start an aim drag and fire a
		// shot on release — the button would shoot.
		if (InteractTouchIndex == INDEX_NONE && InteractRect.IsInside(Position))
		{
			InteractTouchIndex = Index;
			bInteractTouchStillDown = true;
			continue;
		}

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
	if (!bInteractTouchStillDown)
	{
		InteractTouchIndex = INDEX_NONE;
	}

	// Release of the aim thumb is the fire signal — never auto-fire (spec §9).
	bAimReleasedThisFrame = bWasAiming && !AimStick.bActive;
}

void ASarkoPlayerController::UpdateInteract()
{
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		bInteractHeld = false;
		HeldContainerIndex = INDEX_NONE;
		InteractTarget = nullptr;
		return;
	}

	// One-time gather. Containers are spawned from the map file at level start,
	// never added or removed, so re-running an actor iterator every frame would
	// be pure waste on a phone. The cache is not sealed until it has actually
	// found something, because on a client the containers spawn when Seed
	// replicates, which can be several frames after the first PlayerTick.
	if (!bContainersCached)
	{
		CachedContainers.Reset();
		for (TActorIterator<ASarkoLootContainer> It(GetWorld()); It; ++It)
		{
			CachedContainers.Add(*It);
		}
		bContainersCached = CachedContainers.Num() > 0;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FVector PawnLocation = Pawn->GetActorLocation();
	const bool bAlive = Pawn->HealthComponent && !Pawn->HealthComponent->IsDead();

	// Nearest openable container. No allocation: this walks a cached array and
	// keeps one pointer and one float.
	ASarkoLootContainer* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<ASarkoLootContainer>& Weak : CachedContainers)
	{
		ASarkoLootContainer* Container = Weak.Get();
		if (!Container)
		{
			continue;
		}
		if (!SarkoLoot::CanInteract(PawnLocation, Container->GetActorLocation(),
				Settings.InteractRadiusUU, bAlive, Container->IsLooted()))
		{
			continue;
		}
		const float DistanceSquared = FVector2D(
			PawnLocation.X - Container->GetActorLocation().X,
			PawnLocation.Y - Container->GetActorLocation().Y).SizeSquared();
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Container;
		}
	}
	InteractTarget = Best;

	// Held state: the touch button, or E on a desktop. E exists because that is
	// how this game is actually played during development, and a mouse cannot
	// hold a virtual button and a movement stick at once.
	bool bHeld = InteractTouchIndex != INDEX_NONE;
#if !UE_BUILD_SHIPPING
	bHeld = bHeld || IsInputKeyDown(EKeys::E);
#endif

	const int32 BestIndex = Best ? Best->ContainerIndex : INDEX_NONE;
	if (!bHeld || BestIndex == INDEX_NONE)
	{
		// Released, or walked away / emptied the crate while still holding.
		if (bInteractHeld)
		{
			Pawn->RequestCancelLoot();
		}
		bInteractHeld = false;
		HeldContainerIndex = INDEX_NONE;
		return;
	}

	// A fresh press, or the same press now nearest a different crate — the
	// latter matters because a container that has just been emptied drops out of
	// the candidate set while the finger is still down, and standing between two
	// crates should then start on the second one rather than wait for a release.
	if (!bInteractHeld || HeldContainerIndex != BestIndex)
	{
		Pawn->RequestBeginLoot(BestIndex);
		HeldContainerIndex = BestIndex;
	}
	bInteractHeld = true;
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

void ASarkoPlayerController::SarkoOverview()
{
#if !UE_BUILD_SHIPPING
	SarkoDebug::FrameWholeSector(*this, GetDefault<USarkoRaidSettings>()->MapExtent);

	// One frame later, so the new camera position is what gets captured.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ConsoleCommand(TEXT("HighResShot 1600x1600"), /*bWriteToLog*/ true);
	}), 0.25f, false);
#endif
}
