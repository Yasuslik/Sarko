#include "Core/SarkoPlayerController.h"

#include "Application/SlateApplicationBase.h"
#include "Combat/SarkoWeapon.h"
#include "Core/SarkoRaidGameMode.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Debug/SarkoOverviewShot.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoLootContainer.h"
#include "Pawn/SarkoCharacter.h"
#include "Pawn/SarkoHealthComponent.h"
#include "Pawn/SarkoSurvival.h"
#include "UI/SarkoInventoryPanel.h"
#include "UI/SarkoUiScale.h"

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

FBox2D SarkoInput::ReloadButtonRect(FBox2D Frame, float PointScale)
{
	const float Size = ReloadButtonSizePt * PointScale;
	const float Right = Frame.Max.X - ThumbColumnRightInsetPt * PointScale;
	const float Bottom = Frame.Max.Y - ReloadButtonBottomPt * PointScale;
	return FBox2D(FVector2D(Right - Size, Bottom - Size), FVector2D(Right, Bottom));
}

FBox2D SarkoInput::InteractButtonRect(FBox2D Frame, float PointScale)
{
	const float Width = InteractButtonWidthPt * PointScale;
	const float Height = InteractButtonHeightPt * PointScale;
	const float Right = Frame.Max.X - ThumbColumnRightInsetPt * PointScale;
	// Measured off the reload button rather than off the frame, so the 12 pt gap
	// is the gap and cannot drift if either size changes.
	const float Bottom = ReloadButtonRect(Frame, PointScale).Min.Y - ThumbButtonGapPt * PointScale;
	return FBox2D(FVector2D(Right - Width, Bottom - Height), FVector2D(Right, Bottom));
}

FVector2D SarkoInput::RightThumbAnchor(FBox2D Frame, float PointScale)
{
	// Bottom-right, in from the corner by roughly where a thumb's tip lands when
	// the hand is holding the phone rather than reaching across it.
	return FVector2D(Frame.Max.X - 90.f * PointScale, Frame.Max.Y - 60.f * PointScale);
}

bool SarkoInput::ShouldFireWhileHeld(FVector2D AimValue, float FireDeadZone)
{
	return AimValue.Size() >= FMath::Max(KINDA_SMALL_NUMBER, FireDeadZone);
}

bool SarkoInput::IsMoveStickSuppressed(bool bContainerPanelOpen)
{
	// See the header: the ONE place, so spec §5's fallback is a one-line change.
	return bContainerPanelOpen;
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

void ASarkoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Before Super, and unconditionally. A viewport widget is not an actor and is
	// not destroyed with the level: left added, the container panel would still be
	// drawn over the shelter menu the raid travels back to.
	RemoveInventoryPanel();
	Super::EndPlay(EndPlayReason);
}

void ASarkoPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdatePanelBinding();
	// After the binding and before anything reads the panel: this is the one
	// place widgets are created and destroyed, and it is deliberately on the tick
	// rather than inside the Slate event that asked for the change.
	UpdateInventoryPanel();
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

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// HOLD to fire (spec §4.2). There is no fire button: a dedicated one competes
	// with the aim stick for the same thumb, and every mobile shooter that ships
	// both ends up with players using one.
	//
	// Throttled to the weapon's own interval on this side as well as the
	// server's, because RequestFire is a reliable RPC and a held stick would
	// otherwise send one per frame. The server's rate limit is unchanged and
	// still the authority; this is politeness.
	if (AimStick.bActive && SarkoInput::ShouldFireWhileHeld(AimValue, Settings.AimFireDeadZone))
	{
		if (Now - LastLocalFireSeconds >= Settings.MinFireIntervalSeconds)
		{
			LastLocalFireSeconds = Now;
			bAimFiredThisHold = true;
			Pawn->RequestFire();
		}
	}

	// FLICK: a quick tap that never crossed the dead zone still fires once, on
	// release — the behaviour this game already had, kept deliberately as the
	// aimed single shot. A hold that has already fired gets no bonus shot when
	// the thumb finally lifts.
	if (bAimReleasedThisFrame && !bAimFiredThisHold)
	{
		LastLocalFireSeconds = Now;
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
	// hits zero and must be absorbed by the CanFire() gate — which since spec §3
	// dry-clicks and does NOTHING else, rather than starting a reload — instead
	// of crashing or silently firing.
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
	// it is not drawn is a button that misses. SarkoInput::InteractButtonRect is
	// the ONE authority both consult, and it takes no game state at all: the rect
	// never shifts, because the container panel moved to the other half of the
	// screen (spec §4.5) and there is nothing left for it to avoid. The function
	// that used to choose between two rects is gone with the second rect.
	const FBox2D SafeFrame = SarkoInput::SafeFrame(Viewport);
	const float PointScale = SarkoUI::PointScaleForViewport(Viewport);
	const FBox2D InteractRect = SarkoInput::InteractButtonRect(SafeFrame, PointScale);
	const FBox2D ReloadRect = SarkoInput::ReloadButtonRect(SafeFrame, PointScale);
	bool bInteractTouchStillDown = false;
	bool bReloadTouchStillDown = false;

	// Read once per frame, from the pawn's own mirror of what is open. The panel
	// is a client-side view; this is a client-side input rule; neither needs the
	// server's opinion.
	const ASarkoCharacter* PanelPawn = Cast<ASarkoCharacter>(GetPawn());
	const bool bMoveSuppressed = SarkoInput::IsMoveStickSuppressed(
		PanelPawn && PanelPawn->GetOpenContainerIndex() != INDEX_NONE);

	// Dropped on the frame suppression begins, not merely ignored: a stick left
	// active would keep its last deflection and the pawn would walk on through
	// the whole loot, which is the exact opposite of the intent.
	if (bMoveSuppressed && MoveTouchIndex != INDEX_NONE)
	{
		MoveStick = FSarkoTouchStick();
		MoveTouchIndex = INDEX_NONE;
	}

	// FOUR fingers now, not three: movement, aim, the interact button and the
	// reload button are four separate holds, and a player backing away from a bot
	// while opening a crate and reloading is holding all four at once. Each slot
	// is a stable identity for one held finger (UE keeps a finger in the same slot
	// from press to release), so once a slot is bound to a stick — or to a button
	// — it keeps driving that thing regardless of where its finger currently sits;
	// classification happens once, at press time.
	for (int32 Index = 0; Index < 4; ++Index)
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

		if (Index == ReloadTouchIndex)
		{
			if (bPressed)
			{
				bReloadTouchStillDown = true;
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

		// The reload button, for the same reason and one step earlier in the
		// thumb's arc. Claimed before stick classification: without this, pressing
		// it would also start an aim drag — and with hold-to-fire that means the
		// reload button shoots.
		if (ReloadTouchIndex == INDEX_NONE && ReloadRect.IsInside(Position))
		{
			ReloadTouchIndex = Index;
			bReloadTouchStillDown = true;
			// The press edge IS the reload. A hold does nothing more, because
			// there is nothing more for it to do.
			if (ASarkoCharacter* ReloadPawn = Cast<ASarkoCharacter>(GetPawn()))
			{
				ReloadPawn->RequestReload();
			}
			continue;
		}

		if (SarkoInput::IsLeftHalf(Position, Viewport))
		{
			// Suppressed: the touch is consumed by nothing. It is NOT reclassified
			// to the aim stick — a finger landing on the left while looting must
			// not start aiming, and with hold-to-fire that would also start
			// shooting.
			//
			// Reversible with no state to repair: when the panel closes this goes
			// false on the next tick and the next left-half touch-DOWN re-anchors
			// the stick normally. A finger already down when the panel closed
			// drives nothing until it is lifted and pressed again, which is
			// correct — a stick springing to life under a resting finger would
			// start the pawn walking in whatever direction it happened to be.
			if (!bMoveSuppressed && MoveTouchIndex == INDEX_NONE)
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
				// A new hold has fired nothing yet, so a flick that never leaves
				// the dead zone still gets its one shot on release.
				bAimFiredThisHold = false;
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
	if (!bReloadTouchStillDown)
	{
		ReloadTouchIndex = INDEX_NONE;
	}

	// Release of the aim thumb is the FLICK's fire signal. Holding it past the
	// dead zone fires continuously (spec §4.2); PlayerTick owns both.
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

	// A panel is open: the interact button is the CLOSE button now, so a press
	// closes rather than starting a channel on whatever crate is nearest. One
	// control, two jobs, and the thumb already knows where it is.
	if (Pawn->GetOpenContainerIndex() != INDEX_NONE)
	{
		if (bHeld && !bInteractHeld)
		{
			Pawn->RequestCloseContainer();
		}
		bInteractHeld = bHeld;
		HeldContainerIndex = INDEX_NONE;
		return;
	}

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

void ASarkoPlayerController::UpdatePanelBinding()
{
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (Pawn == BoundPawn.Get())
	{
		return;
	}

	// Unbind the old pawn before binding the new one. A multicast delegate on a
	// destroyed actor is gone with it, but a *possession change* between two live
	// pawns would otherwise leave this controller refreshing a panel from a pawn
	// it no longer drives.
	if (ASarkoCharacter* Previous = BoundPawn.Get())
	{
		Previous->OnContainerViewChanged.Remove(ContainerViewHandle);
		Previous->OnTakeRefused.Remove(TakeRefusedHandle);
	}
	ContainerViewHandle.Reset();
	TakeRefusedHandle.Reset();
	BoundPawn = Pawn;

	// A pawn swap is also a panel that is now about somebody else's crate.
	RemoveInventoryPanel();

	if (!Pawn)
	{
		return;
	}
	ContainerViewHandle = Pawn->OnContainerViewChanged.AddUObject(
		this, &ASarkoPlayerController::HandleContainerViewChanged);
	TakeRefusedHandle = Pawn->OnTakeRefused.AddUObject(
		this, &ASarkoPlayerController::HandleTakeRefused);

	// The contents may already have arrived — the first RPC can beat the first
	// tick after possession — so ask once rather than waiting for a change.
	HandleContainerViewChanged();
}

void ASarkoPlayerController::HandleContainerViewChanged()
{
	// Deferred to the next tick, and NEVER done inline. This delegate can fire
	// from inside SButton::ExecuteOnClick: tapping a cell calls RequestTakeItem,
	// and in a standalone or listen-server game the "RPC" runs synchronously, so
	// the contents change and this arrives while the button is still mid-click.
	// Rebuilding the grid here destroys that very button, and ExecuteOnClick then
	// calls AsShared() on a freed widget — Assertion failed:
	// DoesSharedInstanceExist() in SharedPointer.h, a hard crash, every single
	// time a player taps a cell. A take-all that empties the crate reaches the
	// panel *removal* down the same path, which is why the whole body moved and
	// not just the Refresh.
	bPanelDirty = true;
}

void ASarkoPlayerController::UpdateInventoryPanel()
{
	if (!bPanelDirty)
	{
		return;
	}
	bPanelDirty = false;

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		RemoveInventoryPanel();
		return;
	}

	if (Pawn->GetOpenContainerIndex() == INDEX_NONE)
	{
		// Closed. 90 ms of fade and then gone — the widget cannot remove itself,
		// because the thing being removed is the thing that is animating.
		if (InventoryPanel.IsValid() && !bPanelExiting)
		{
			bPanelExiting = true;
			InventoryPanel->PlayExit();
			GetWorldTimerManager().SetTimer(PanelExitTimer, this,
				&ASarkoPlayerController::RemoveInventoryPanel, 0.12f, false);
		}
		return;
	}

	// Opening. A panel caught mid-fade is rebuilt rather than revived: its exit
	// curve has already run and reviving it would leave it permanently invisible.
	if (bPanelExiting)
	{
		RemoveInventoryPanel();
	}

	if (!InventoryPanel.IsValid())
	{
		// Local only, and null in a headless run: a widget belongs to a viewport,
		// and a remote controller has none.
		UGameViewportClient* Viewport = (IsLocalController() && GetWorld()) ? GetWorld()->GetGameViewport() : nullptr;
		if (!Viewport)
		{
			return;
		}
		InventoryPanel = SNew(SSarkoInventoryPanel).Pawn(Pawn);
		Viewport->AddViewportWidgetContent(InventoryPanel.ToSharedRef());

		// Deliberately NO SetInputMode. See the panel's header: FInputModeUIOnly
		// would kill every touch, stick, shot and loot press in the raid.
	}
	InventoryPanel->Refresh();
}

void ASarkoPlayerController::HandleTakeRefused(int32 SlotIndex, ESarkoTakeRefusal Reason)
{
	if (InventoryPanel.IsValid())
	{
		InventoryPanel->PlayRefusal(SlotIndex, Reason);
	}
}

void ASarkoPlayerController::RemoveInventoryPanel()
{
	GetWorldTimerManager().ClearTimer(PanelExitTimer);
	bPanelExiting = false;
	if (!InventoryPanel.IsValid())
	{
		return;
	}
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(InventoryPanel.ToSharedRef());
	}
	InventoryPanel.Reset();
}

void ASarkoPlayerController::SarkoDebugLoot(int32 Count)
{
#if !UE_BUILD_SHIPPING
	// A screenshot of an empty bag proves nothing about a layout whose whole
	// question is "are twelve cells with counts on them legible or mush". This
	// puts a mixed haul in, spanning every category the palette has a hue for,
	// so the frame answers that question instead of dodging it.
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn || !Pawn->BackpackComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugLoot: no possessed pawn with a backpack"));
		return;
	}

	// The bag itself first, or the capacity is four cells and most of these are
	// refused — which is a different screenshot from the one being asked for.
	//
	// Count 0 means NO BAG, not "a bag with nothing in it": the dimmed
	// НЕМАЄ РЮКЗАКА page is a state the panel has to be photographed in, and it
	// is the only state a worn bag makes unreachable.
	if (Count > 0)
	{
		Pawn->BackpackComponent->EquipBackpack(SarkoLoot::BackpackItemId);
	}

	// Ordered so the first four cells are four different hues: a four-pocket
	// pawn photographed with Count 4 still shows the palette doing its job.
	// `water_bottle` sits third since the survival stage — Consumable is a hue
	// the palette gained and had no representative for, and it is also the one
	// category with a VERB, so a low Count now reaches a cell that can be tapped.
	// It replaced `painkillers`, which duplicated the medkit's hue and no longer
	// appears anywhere on the tutorial route.
	static const FName Mixed[] = {
		TEXT("pistol"), TEXT("ammo_9mm"), TEXT("water_bottle"), TEXT("medkit"),
		TEXT("toolbox"), TEXT("canned_food"), TEXT("scrap_metal"), TEXT("wheel_small"),
		TEXT("bandage"), TEXT("vodka"), TEXT("copper_wire"), TEXT("chain"),
	};
	static const int32 Quantities[] = { 1, 47, 2, 3, 1, 3, 9, 2, 5, 2, 12, 1 };

	TArray<FSarkoItemStack> Slots;
	const int32 Wanted = FMath::Clamp(Count, 0, UE_ARRAY_COUNT(Mixed));
	for (int32 Index = 0; Index < Wanted; ++Index)
	{
		FSarkoItemStack Stack;
		Stack.Item = Mixed[Index];
		Stack.Quantity = Quantities[Index];
		Slots.Add(Stack);
	}
	Pawn->BackpackComponent->SetSlots(Slots);
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugLoot: bag now %d/%d"),
		Pawn->BackpackComponent->GetUsedCells(), Pawn->BackpackComponent->GetCellCount());
#endif
}

void ASarkoPlayerController::SarkoOpenNearestContainer()
{
#if !UE_BUILD_SHIPPING
	// Retried rather than done once: -ExecCmds is queued at engine init, and the
	// raid's authoritative seed (which gates every loot request) plus the channel
	// itself both land several frames later. The pump stops as soon as the pawn
	// reports a container open.
	GetWorldTimerManager().SetTimer(DebugOpenTimer, this,
		&ASarkoPlayerController::TickDebugOpen, 0.5f, true, 0.5f);
#endif
}

#if !UE_BUILD_SHIPPING
void ASarkoPlayerController::TickDebugOpen()
{
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		return;
	}
	if (Pawn->GetOpenContainerIndex() != INDEX_NONE)
	{
		GetWorldTimerManager().ClearTimer(DebugOpenTimer);
		return;
	}
	// Already channelling: re-requesting would reset the channel's start time
	// every half second and it would never complete.
	if (Pawn->GetLootChannelIndex() != INDEX_NONE)
	{
		return;
	}

	ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState || !RaidState->IsLootable())
	{
		return;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	int32 BestIndex = INDEX_NONE;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<ASarkoLootContainer>& Weak : RaidState->GetContainers())
	{
		const ASarkoLootContainer* Container = Weak.Get();
		if (!Container || RaidState->IsContainerEmptied(Container->ContainerIndex))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(PawnLocation, Container->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = Container->ContainerIndex;
		}
	}
	if (BestIndex == INDEX_NONE)
	{
		return;
	}

	// The crate's contents are overwritten with four different categories so the
	// frame answers "are these hues distinguishable at cell size" rather than
	// whatever one tier's roll happened to produce. Server-side and through the
	// game mode's own store, so the take path afterwards is the real one.
	if (ASarkoRaidGameMode* GameMode = GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>())
	{
		if (TArray<FSarkoItemStack>* Inventory = GameMode->OpenContainerAt(BestIndex))
		{
			static const FName Contents[] = { TEXT("pistol"), TEXT("ammo_9mm"), TEXT("medkit"), TEXT("toolbox") };
			static const int32 Amounts[] = { 1, 24, 2, 1 };
			Inventory->Reset();
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Contents); ++Index)
			{
				FSarkoItemStack Stack;
				Stack.Item = Contents[Index];
				Stack.Quantity = Amounts[Index];
				Inventory->Add(Stack);
			}
		}
	}

	Pawn->RequestBeginLoot(BestIndex);
}

void ASarkoPlayerController::TickDebugTap()
{
	if (InventoryPanel.IsValid() && InventoryPanel->SimulateTapContainerCell(DebugTapSlot))
	{
		// Logged with the world time, because the ONE thing a still frame cannot
		// show is a 240 ms pulse, and knowing the instant it started is the
		// difference between "the shot was mistimed" and "the glow never drew".
		UE_LOG(LogTemp, Display, TEXT("SarkoTapContainerCell: tapped cell %d at t=%.3f"),
			DebugTapSlot, GetWorld() ? GetWorld()->GetTimeSeconds() : -1.f);
		GetWorldTimerManager().ClearTimer(DebugTapTimer);

		// Chained off the tap, not off engine start: the refusal pulse is 240 ms
		// long and the tap lands whenever the loot channel finishes, so a shutter
		// timed from boot photographs the transient only by luck.
		if (DebugTapShotDelay > 0.f)
		{
			GetWorldTimerManager().SetTimer(DebugShotTimer, this,
				&ASarkoPlayerController::TakeDebugShot, DebugTapShotDelay, false);
		}
	}
}

void ASarkoPlayerController::TakeDebugShot()
{
	// `Shot showui` and NOT HighResShot: HighResShot goes through the scene
	// renderer and captures no Slate at all, so the PNG comes out with no panel
	// on it — which looks exactly like a panel that failed to draw.
	ConsoleCommand(TEXT("Shot showui"), /*bWriteToLog*/ true);
}
#endif

void ASarkoPlayerController::SarkoTapContainerCell(int32 SlotIndex, float ShotDelay)
{
#if !UE_BUILD_SHIPPING
	// A headless run has no fingers, and the panel does not exist yet when this
	// command is queued. Retried until the cell is there and enabled.
	DebugTapSlot = SlotIndex;
	DebugTapShotDelay = ShotDelay;
	GetWorldTimerManager().SetTimer(DebugTapTimer, this,
		&ASarkoPlayerController::TickDebugTap, 0.5f, true, 0.5f);
#endif
}

void ASarkoPlayerController::SarkoInventoryShot(float Delay)
{
#if !UE_BUILD_SHIPPING
	GetWorldTimerManager().SetTimer(DebugShotTimer, this,
		&ASarkoPlayerController::TakeDebugShot, FMath::Max(0.1f, Delay), false);
#endif
}

void ASarkoPlayerController::SarkoDebugAmmo(int32 Rounds)
{
#if !UE_BUILD_SHIPPING
	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugAmmo: no possessed pawn with a weapon"));
		return;
	}
	Pawn->WeaponComponent->ResetForTest(FMath::Max(0, Rounds));
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugAmmo: magazine now %d"),
		Pawn->WeaponComponent->GetAmmoInMagazine());
#endif
}

void ASarkoPlayerController::SarkoDebugSurvival(float Food, float Water, float Damage, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	// -ExecCmds runs its whole list at engine init, so a step that has to happen
	// LATER carries its own delay. Rescheduled through this same function, so the
	// deferred path and the immediate one cannot drift apart.
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, Food, Water, Damage]() { SarkoDebugSurvival(Food, Water, Damage, 0.f); }),
			DelaySeconds, /*bLoop*/ false);
		return;
	}

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn || !Pawn->SurvivalComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugSurvival: no possessed pawn with survival meters"));
		return;
	}
	Pawn->SurvivalComponent->ResetForTest(Food, Water);
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugSurvival: food %.0f%%, water %.0f%%"),
		Pawn->SurvivalComponent->GetFoodExact(), Pawn->SurvivalComponent->GetWaterExact());

	// Applied through the real damage path, so it stamps the combat clock exactly
	// as a bullet would — which is the half of the regeneration rule that a
	// direct health write would skip.
	if (Damage > 0.f && Pawn->HealthComponent)
	{
		Pawn->HealthComponent->ApplyDamage(Damage, nullptr);
		UE_LOG(LogTemp, Display, TEXT("SarkoDebugSurvival: took %.0f damage, health now %.0f"),
			Damage, Pawn->HealthComponent->GetHealth());
	}
#endif
}

void ASarkoPlayerController::SarkoTapCarryCell(int32 SlotIndex, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	if (DelaySeconds > 0.f)
	{
		// The panel does not exist yet when this is queued: the raid's session, the
		// 1.5 s loot channel and the panel's own construction all land seconds in.
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, SlotIndex]() { SarkoTapCarryCell(SlotIndex, 0.f); }), DelaySeconds, /*bLoop*/ false);
		return;
	}

	if (!InventoryPanel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoTapCarryCell: no panel is open — open a container first"));
		return;
	}
	const bool bTapped = InventoryPanel->SimulateTapCarryCell(SlotIndex);
	UE_LOG(LogTemp, Display, TEXT("SarkoTapCarryCell: cell %d %s"),
		SlotIndex, bTapped ? TEXT("tapped") : TEXT("is not a tappable consumable"));
#endif
}

void ASarkoPlayerController::SarkoDebugStandInZone(int32 ZoneIndex, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, ZoneIndex]() { SarkoDebugStandInZone(ZoneIndex, 0.f); }), DelaySeconds, /*bLoop*/ false);
		return;
	}

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	ASarkoRaidGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASarkoRaidGameMode>() : nullptr;
	if (!Pawn || !GameMode || !GameMode->CachedDefinition.Extractions.IsValidIndex(ZoneIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugStandInZone: no pawn, or no zone %d on this map"), ZoneIndex);
		return;
	}
	// The pad's authored centre, read from the map the server is running rather
	// than typed into a script that would go stale the moment the zone moves.
	const FSarkoExtractionSpot& Zone = GameMode->CachedDefinition.Extractions[ZoneIndex];
	Pawn->SetActorLocation(Zone.Location + FVector(0.f, 0.f, 150.f), /*bSweep*/ false);
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugStandInZone: standing on zone %d ('%s') at %s"),
		ZoneIndex, *Zone.Name, *Zone.Location.ToCompactString());
#endif
}

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
