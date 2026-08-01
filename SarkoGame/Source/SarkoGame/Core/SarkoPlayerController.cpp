#include "Core/SarkoPlayerController.h"

#include "Application/SlateApplicationBase.h"
#include "Combat/SarkoWeapon.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Debug/SarkoOverviewShot.h"
#include "HAL/IConsoleManager.h"
#include "Loot/SarkoLootContainer.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"

bool SarkoInput::IsLeftHalf(FVector2D ScreenPosition, FVector2D ViewportSize)
{
	return ScreenPosition.X < ViewportSize.X * 0.5f;
}

namespace
{
	/**
	 * Puts a landscape iPhone's cutouts onto a screen that has none, so the safe
	 * area is visible in an offscreen shot taken on a Mac.
	 *
	 * The fractions are an iPhone 14/15 Pro in landscape reduced to ratios: 59 pt
	 * of the 852-pt long edge on each side (0.069) and 21 pt of the 393-pt short
	 * edge along the bottom (0.053). Ratios and not points because the offscreen
	 * shots are taken at physical pixel sizes, where a point constant would mean
	 * nothing.
	 *
	 * Off by default and never consulted on a device, where the real insets are
	 * available and this would be a worse guess than the truth.
	 */
	TAutoConsoleVariable<bool> CVarSarkoSafeAreaDebugPhoneLandscape(
		TEXT("sarko.SafeArea.DebugPhoneLandscape"), false,
		TEXT("Pretend the viewport is a landscape iPhone with a Dynamic Island and a home indicator."),
		ECVF_Cheat);
}

FBox2D SarkoInput::SafeFrame(FVector2D ViewportSize)
{
	FMargin Safe(0.f);
	if (FSlateApplicationBase::IsInitialized())
	{
		Safe = FMargin(0.f);
		FSlateApplicationBase::Get().GetSafeZoneSize(Safe, ViewportSize);
	}

	if (CVarSarkoSafeAreaDebugPhoneLandscape.GetValueOnAnyThread())
	{
		Safe = FMargin(ViewportSize.X * 0.069f, 0.f, ViewportSize.X * 0.069f, ViewportSize.Y * 0.053f);
	}

	const FBox2D Frame(
		FVector2D(Safe.Left, Safe.Top),
		FVector2D(ViewportSize.X - Safe.Right, ViewportSize.Y - Safe.Bottom));

	// A degenerate frame would put every readout on top of itself in a corner,
	// which is worse than ignoring the insets. Reached when the viewport is
	// smaller than the insets, i.e. a window a few pixels wide during a resize.
	return (Frame.Min.X < Frame.Max.X && Frame.Min.Y < Frame.Max.Y)
		? Frame
		: FBox2D(FVector2D::ZeroVector, ViewportSize);
}

FBox2D SarkoInput::InteractButtonRect(FBox2D Frame)
{
	// Scaled to the shorter axis so the button is the same physical size on a
	// phone and in a window, with a floor so it never drops below a thumb.
	const FVector2D FrameSize = Frame.GetSize();
	const float Size = FMath::Max(96.f, FMath::Min(FrameSize.X, FrameSize.Y) * 0.14f);
	const float Right = Frame.Max.X - Size - Size * 0.35f;
	const float Top = Frame.GetCenter().Y - Size * 0.5f;
	return FBox2D(FVector2D(Right, Top), FVector2D(Right + Size, Top + Size));
}

FBox2D SarkoInput::InteractButtonRect(FVector2D ViewportSize)
{
	return InteractButtonRect(FBox2D(FVector2D::ZeroVector, ViewportSize));
}

ASarkoPlayerController::ASarkoPlayerController()
{
	bShowMouseCursor = false;
	// Touch is the only input path in this slice.
	DefaultMouseCursor = EMouseCursor::None;
}

void ASarkoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// See the header. The shelter's FInputModeUIOnly lives on the ULocalPlayer's
	// viewport client, which outlives the travel, so a raid entered from the
	// shelter starts with bIgnoreInput still set and every input path dead. This
	// is the raid's own claim on its input mode, made regardless of what the
	// previous screen left behind.
	//
	// Local only: a remote controller has no viewport to set a mode on. Also the
	// reason this is a no-op in a headless automation run, where the local
	// player's viewport client has no game viewport widget at all.
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
	}
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

	// Spec §4.5: on a finished raid input is frozen and the summary is all that
	// is left. Frozen here rather than by unpossessing the pawn, so the HUD can
	// still read the backpack it is about to list. This is only the client half
	// of the freeze — the server disables the pawn's movement and refuses its
	// aim/fire/loot RPCs (ASarkoRaidGameMode::FinishRaid), because a client that
	// keeps sending is precisely the case this must survive.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && RaidState->IsRaidFinished())
	{
		Pawn->SetMoveIntent(FVector2D::ZeroVector);
		Pawn->SetAimIntent(FVector2D::ZeroVector, false);
		bInteractHeld = false;
		InteractTarget = nullptr;
		HeldContainerIndex = INDEX_NONE;
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

	// The same rect the HUD draws, safe area and all — a button hit-tested where
	// it is not drawn is a button that misses.
	const FBox2D InteractRect = SarkoInput::InteractButtonRect(SarkoInput::SafeFrame(Viewport));
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
	// UpdateInteract runs ahead of PlayerTick's freeze check, so the same
	// condition is applied here: without it a finished raid would keep drawing a
	// loot prompt and re-issuing channel requests the server now refuses.
	//
	// A missing game state is treated the same as a finished raid rather than
	// waved through: it owns both the container registry read below and the looted
	// bits, so without it there is nothing to interact with and no way to know
	// whether it has been emptied already.
	//
	// IsLootable() also covers the other end: before the raid's authoritative seed
	// arrives the server refuses every loot request, so a prompt drawn during that
	// round trip would only invite a press that cannot work.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn || !RaidState || !RaidState->IsLootable())
	{
		bInteractHeld = false;
		HeldContainerIndex = INDEX_NONE;
		InteractTarget = nullptr;
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FVector PawnLocation = Pawn->GetActorLocation();
	const bool bAlive = Pawn->HealthComponent && !Pawn->HealthComponent->IsDead();

	// Nearest openable container. No allocation and no world scan: the containers
	// register themselves with the game state at BeginPlay (which is also what
	// lets a replicated state byte recolour them), so this walks a list that is
	// already maintained. That registry replaces a cached TActorIterator scan
	// which, on a map with no containers at all — a bridge.json that failed to
	// load, say — could never seal itself and re-ran a heap-allocating iterator
	// over the whole world every single frame, forever.
	//
	// ASarkoLootContainer::IsEmptied() would resolve the world and the game state
	// per container per frame; RaidState is already in hand, so the state byte is
	// read straight off it with one lookup for the whole loop. Gated on EMPTIED
	// and not on opened: a crate whose contents did not fit is still worth a
	// prompt, which is the whole point of the three-state byte.
	ASarkoLootContainer* Best = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<ASarkoLootContainer>& Weak : RaidState->GetContainers())
	{
		ASarkoLootContainer* Container = Weak.Get();
		if (!Container)
		{
			continue;
		}
		if (!SarkoLoot::CanInteract(PawnLocation, Container->GetActorLocation(),
				Settings.InteractRadiusUU, bAlive, RaidState->IsContainerEmptied(Container->ContainerIndex)))
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
