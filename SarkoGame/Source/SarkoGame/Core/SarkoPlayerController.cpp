#include "Core/SarkoPlayerController.h"

#include "Application/SlateApplicationBase.h"
#include "Combat/SarkoWeapon.h"
#include "Core/SarkoRaidGameMode.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "AI/SarkoEnemyCharacter.h"
#include "Debug/SarkoOverviewShot.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerInput.h"
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

SarkoInput::ESarkoAimZone SarkoInput::AimZoneFor(FVector2D AimValue, float MoveDeadZone, float FireThreshold)
{
	const float Deflection = AimValue.Size();

	// Outside in, because the zones nest and the outer one wins. Both bounds are
	// floored at KINDA_SMALL_NUMBER so a setting of zero cannot make a thumb that
	// is not touching anything read as a thumb that is firing.
	if (Deflection >= FMath::Max(KINDA_SMALL_NUMBER, FireThreshold))
	{
		return ESarkoAimZone::Fire;
	}
	if (Deflection >= FMath::Max(KINDA_SMALL_NUMBER, MoveDeadZone))
	{
		return ESarkoAimZone::Aim;
	}
	return ESarkoAimZone::Rest;
}

bool SarkoInput::ShouldFireWhileHeld(FVector2D AimValue, float FireDeadZone)
{
	// Asked of the zone classifier and not of the vector, so there is one place
	// the boundary lives — the same place ASarkoHUD::DrawStick draws it. The move
	// dead zone is passed as zero because this question does not care: Rest and
	// Aim are both "do not shoot", and only the outer bound separates them from
	// the answer.
	return AimZoneFor(AimValue, /*MoveDeadZone*/ 0.f, FireDeadZone) == ESarkoAimZone::Fire;
}

float SarkoInput::StickRadiusPxForViewport(FVector2D ViewportSize)
{
	// The SAME point scale the HUD lays itself out with, so the ring drawn around
	// the thumb and the rule that ring pictures cannot come from two numbers.
	return StickRadiusPt * SarkoUI::PointScaleForViewport(ViewportSize);
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

	// The loot panel is a considered pose; a bullet ends it. See the header.
	UpdateLootPanelUnderFire(*Pawn);

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
	bool bDebugMoved = false;
#if !UE_BUILD_SHIPPING
	bKeyboardMoved = ApplyDesktopTestInput(*Pawn, CameraYaw);
	// The headless stick (SarkoDebugMove) has to be applied HERE, every frame, for
	// the same reason the keyboard is: this function reassigns the move intent
	// unconditionally from MoveStick, so an intent written once from a console
	// command survives exactly zero frames. That is why the first attempt at
	// verifying "walking is quiet" produced no movement noise at all — not because
	// the model was silent, but because the pawn never moved.
	bDebugMoved = ApplyDebugMoveInput(*Pawn);
#endif

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	if (!bKeyboardMoved && !bDebugMoved)
	{
		const FVector2D MoveValue = MoveStick.Value();
		// The first hint's dismissal (see bEverMoved): the player has moved when
		// the pawn has actually walked, i.e. past the same dead zone the movement
		// itself is gated on — not merely when a finger touched the left half.
		bEverMoved |= MoveValue.Size() >= Settings.MoveStickDeadZone;
		const FVector2D MoveDirection = SarkoAim::StickToWorldDirection(MoveValue, CameraYaw);
		Pawn->SetMoveIntent(MoveDirection * MoveValue.Size());
	}

	const FVector2D AimValue = AimStick.Value();
	Pawn->SetAimIntent(SarkoAim::StickToWorldDirection(AimValue, CameraYaw), AimStick.bActive);

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// THE AIM STICK'S SECOND ZONE, and the only thing in this game that fires a
	// round (spec §4.2). There is no fire button: a dedicated one competes with
	// the aim stick for the same thumb, and every mobile shooter that ships both
	// ends up with players using one.
	//
	// Inside SarkoInput::AimZoneFor's Fire boundary the stick aims and nothing
	// else, which is a sentence this control scheme could not say until now — the
	// threshold was 0.35, i.e. 18 pt of a 52 pt stick, so any deflection large
	// enough to point the pawn at something was already large enough to shoot at
	// it. The first phone playtest spent a whole eight-round magazine finding that
	// out, at 2600 uu of noise a round.
	//
	// Nothing here is edge-triggered: crossing the boundary fires on the crossing
	// FRAME (LastLocalFireSeconds is a free-running clock, and a thumb arriving
	// from the Aim zone has not fired for a while), then repeats at the weapon's
	// interval. A shot has to feel like it left when the thumb got there. The cost
	// is that the boundary must be somewhere a thumb does not wander, which is the
	// whole point of moving it to 0.70 and of drawing it.
	//
	// Throttled to the weapon's own interval on this side as well as the
	// server's, because RequestFire is a reliable RPC and a held stick would
	// otherwise send one per frame. The server's rate limit is unchanged and
	// still the authority; this is politeness.
	//
	// AND THERE IS NO SECOND PATH. Releasing the thumb fires nothing, whatever it
	// was doing — see the note where SarkoInput::ShouldFireOnRelease used to be
	// declared. One rule: past the ring you are shooting, inside it you are not.
	// A single aimed shot is a push past the ring and a lift, which fires exactly
	// once because the next round is 0.15 s away.
	if (AimStick.bActive && SarkoInput::ShouldFireWhileHeld(AimValue, Settings.AimFireDeadZone))
	{
		if (Now - LastLocalFireSeconds >= Settings.MinFireIntervalSeconds)
		{
			LastLocalFireSeconds = Now;
			bEverFired = true;
			Pawn->RequestFire();
		}
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

void ASarkoPlayerController::CheatReload(float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	// -ExecCmds runs its whole list at engine init, so a press that has to happen
	// LATER carries its own delay — the same shape SarkoDebugSurvival and
	// SarkoDebugStandInZone use, and for the same reason.
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this]() { CheatReload(0.f); }), DelaySeconds, /*bLoop*/ false);
		return;
	}

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn || !Pawn->WeaponComponent || !Pawn->BackpackComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("CheatReload: no possessed pawn with a weapon and a bag"));
		return;
	}

	// Logged BEFORE the press only. What the press did is FinishReload's and
	// StartReload's to say — this line exists so the log carries the state the
	// press was made against, which is the half a reader cannot reconstruct.
	UE_LOG(LogTemp, Display, TEXT("CheatReload: pressing with magazine=%d reserve=%d"),
		Pawn->WeaponComponent->GetAmmoInMagazine(),
		Pawn->BackpackComponent->CountItem(SarkoLoot::AmmoItemId));
	Pawn->RequestReload();
#endif
}

void ASarkoPlayerController::CheatDrainAndReload(int32 Cycles, float IntervalSeconds)
{
#if !UE_BUILD_SHIPPING
	const int32 Remaining = Cycles;
	if (Remaining <= 0)
	{
		return;
	}

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("CheatDrainAndReload: no possessed pawn with a weapon"));
		return;
	}

	// The magazine is zeroed rather than fired away: RequestFire needs a target
	// worth tracing at and a rate limit's worth of frames per round, and neither
	// has anything to do with the transfer under observation.
	Pawn->WeaponComponent->ResetForTest(0);
	CheatReload(0.f);

	const float Interval = FMath::Max(0.1f, IntervalSeconds);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
		[this, Remaining, Interval]() { CheatDrainAndReload(Remaining - 1, Interval); }),
		Interval, /*bLoop*/ false);
#endif
}

#if !UE_BUILD_SHIPPING
void ASarkoPlayerController::ScheduleTouch(int32 FingerIndex, uint8 TouchType, FVector2D Position, float AfterSeconds)
{
	// Through UPlayerInput, which is where the iOS layer delivers touches too.
	// UPlayerInput::Touches is what APlayerController::GetInputTouchState reads,
	// and a Began/Moved leaves the finger DOWN until an Ended arrives — so a
	// scripted gesture is a handful of events, not one per frame.
	const auto Fire = [this, FingerIndex, TouchType, Position]()
	{
		if (!PlayerInput)
		{
			UE_LOG(LogTemp, Warning, TEXT("SarkoDebugTouch: no PlayerInput to inject into"));
			return;
		}
		// Device 0: IPlatformInputDeviceMapper's default, spelled without it
		// because reaching for the mapper would put an ApplicationCore dependency
		// on this module for one integer, and a debug seam is not worth that.
		PlayerInput->InputTouch(
			FTouchId(FInputDeviceId::CreateFromInternalId(0),
				static_cast<ETouchIndex::Type>(FingerIndex)),
			static_cast<ETouchType::Type>(TouchType), Position, /*Force*/ 1.f, /*Timestamp*/ 0);
	};

	if (AfterSeconds <= 0.f)
	{
		Fire();
		return;
	}

	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, Fire),
		AfterSeconds, /*bLoop*/ false);
}

FVector2D ASarkoPlayerController::DebugThumbAnchor(bool bLeftHalf) const
{
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	const FVector2D Viewport(static_cast<float>(ViewportX), static_cast<float>(ViewportY));
	const FBox2D Frame = SarkoInput::SafeFrame(Viewport);
	const float Scale = SarkoUI::PointScaleForViewport(Viewport);

	const FVector2D Right = SarkoInput::RightThumbAnchor(Frame, Scale);
	// Mirrored, so a debug drag starts where a real thumb would and the stick's
	// whole ring stays inside the frame at either end.
	return bLeftHalf ? FVector2D(Frame.Min.X + (Frame.Max.X - Right.X), Right.Y) : Right;
}

float ASarkoPlayerController::DebugStickRadiusPx() const
{
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	return SarkoInput::StickRadiusPxForViewport(
		FVector2D(static_cast<float>(ViewportX), static_cast<float>(ViewportY)));
}

void ASarkoPlayerController::LogGestureAmmo(const TCHAR* Stage, const FString& Kind)
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	const USarkoWeaponComponent* Weapon = Pawn ? Pawn->WeaponComponent : nullptr;
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugAimGesture[%s] %s: magazine=%d"),
		*Kind, Stage, Weapon ? Weapon->GetAmmoInMagazine() : -1);
}
#endif

void ASarkoPlayerController::SarkoDebugTouchStick(int32 Half, float DirX, float DirY, float Fraction, float HoldSeconds, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, Half, DirX, DirY, Fraction, HoldSeconds]()
			{ SarkoDebugTouchStick(Half, DirX, DirY, Fraction, HoldSeconds, 0.f); }),
			DelaySeconds, /*bLoop*/ false);
		return;
	}

	const bool bLeft = Half == 0;
	const FVector2D Anchor = DebugThumbAnchor(bLeft);
	const float Radius = DebugStickRadiusPx();

	// Screen Y grows DOWNWARD and FSarkoTouchStick::Value flips it, so a caller
	// asking for "up" (+Y) must be dragged toward a smaller screen Y.
	FVector2D Direction(DirX, DirY);
	Direction = Direction.IsNearlyZero() ? FVector2D(0.f, 1.f) : Direction.GetSafeNormal();
	const FVector2D Held = Anchor + FVector2D(Direction.X, -Direction.Y) * Radius * FMath::Clamp(Fraction, 0.f, 2.f);

	// Two fingers, and always the same one per half, so holding both sticks at
	// once — which is what a frame showing both rings needs — is one call each.
	const int32 Finger = bLeft ? 0 : 1;
	UE_LOG(LogTemp, Display,
		TEXT("SarkoDebugTouchStick: %s stick, anchor (%.0f, %.0f), radius %.1f px, held at %.2f of it for %.1fs"),
		bLeft ? TEXT("MOVE") : TEXT("AIM"), Anchor.X, Anchor.Y, Radius, Fraction, HoldSeconds);

	ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Began), Anchor, 0.f);
	ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Moved), Held, 0.1f);
	ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Ended), Held, FMath::Max(0.2f, HoldSeconds));
#endif
}

void ASarkoPlayerController::SarkoDebugAimGesture(FString Kind, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, Kind]() { SarkoDebugAimGesture(Kind, 0.f); }), DelaySeconds, /*bLoop*/ false);
		return;
	}

	const FVector2D Anchor = DebugThumbAnchor(/*bLeftHalf*/ false);
	const float Radius = DebugStickRadiusPx();

	// 0.45 of the radius: past MoveStickDeadZone 0.15 so it counts as a real aim,
	// short of AimFireDeadZone 0.70 so it is squarely in the AIM zone. Every
	// gesture below except "push" lives there, and the ammo count either side of
	// each one is the assertion — all of them must spend nothing.
	const FVector2D Out = Anchor + FVector2D(0.f, -Radius * 0.45f);
	// Past the ring, for the one gesture that is supposed to cost a round.
	const FVector2D Past = Anchor + FVector2D(0.f, -Radius * 0.95f);
	const int32 Finger = 1;

	LogGestureAmmo(TEXT("before"), Kind);
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugAimGesture[%s]: pressing at (%.0f, %.0f), radius %.1f px"),
		*Kind, Anchor.X, Anchor.Y, Radius);

	ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Began), Anchor, 0.f);

	float EndAt = 0.4f;
	if (Kind == TEXT("flick"))
	{
		// Out into the AIM zone, and lifted there. It used to be the one gesture
		// that fired; it is now the one that must NOT, because that band is where
		// every deliberate turn-and-look now ends. Ammo before == ammo after.
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Moved), Out, 0.15f);
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Ended), Out, 0.4f);
	}
	else if (Kind == TEXT("cancel"))
	{
		// Out, then back onto the anchor, then lifted: the abort gesture.
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Moved), Out, 0.15f);
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Moved), Anchor, 0.4f);
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Ended), Anchor, 0.6f);
		EndAt = 0.6f;
	}
	else if (Kind == TEXT("push"))
	{
		// PAST the ring and lifted straight away: the deliberate single shot, and
		// the only gesture in this list that is allowed to cost anything. Exactly
		// one round — the crossing frame fires, and the next is 0.15 s out.
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Moved), Past, 0.15f);
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Ended), Past, 0.25f);
	}
	else
	{
		// "tap": pressed and lifted, never moved. The stray touch.
		ScheduleTouch(Finger, static_cast<uint8>(ETouchType::Ended), Anchor, 0.4f);
	}

	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
		[this, Kind]() { LogGestureAmmo(TEXT("after"), Kind); }), EndAt + 0.3f, /*bLoop*/ false);
#endif
}

void ASarkoPlayerController::UpdateSticks()
{
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	GetViewportSize(ViewportX, ViewportY);
	const FVector2D Viewport(static_cast<float>(ViewportX), static_cast<float>(ViewportY));

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
	// Resolved ONCE per frame and written onto a stick only when it anchors: the
	// radius cannot change while a finger is down, so recomputing it per stick
	// per frame would be a multiply on a tick path for a constant. Every consumer
	// — Value()'s deflection maths, the dead zone and fire threshold that are
	// fractions of it, and the two rings the HUD draws — reads it back off the
	// stick, so there is exactly one number.
	const float StickRadiusPx = SarkoInput::StickRadiusPxForViewport(Viewport);
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
				bEverReloaded = true;
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
				// The one place the point-sized radius becomes pixels for this
				// stick: at anchor, from this frame's viewport.
				MoveStick.RadiusPx = StickRadiusPx;
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
				AimStick.RadiusPx = StickRadiusPx;
				bAimTouchStillDown = true;
				// A stick anchors AT the finger, so a fresh hold starts at zero
				// deflection — in the Rest zone, whatever the previous hold ended
				// on. There is nothing to reset: a touch cannot arrive already
				// past the fire boundary, and no state survives a release that
				// could fire on its own.
			}
		}
	}

	if (!bMoveTouchStillDown)
	{
		MoveStick = FSarkoTouchStick();
		MoveTouchIndex = INDEX_NONE;
	}
	// Nothing is latched off the release edge any more. The controller used to
	// carry the last deflection across the frame the finger lifted, because a
	// flick fired its shot there; the release fires nothing now, so the value has
	// no reader and keeping it would be one more piece of state that could
	// disagree with the stick.
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
}

void ASarkoPlayerController::UpdateLootPanelUnderFire(ASarkoCharacter& Pawn)
{
	const USarkoHealthComponent* Health = Pawn.HealthComponent;
	if (!Health)
	{
		return;
	}

	const int32 Serial = static_cast<int32>(Health->GetDamageSerial());
	if (Serial == LastSeenDamageSerial)
	{
		return;
	}

	// Recorded before it is acted on, and the FIRST observation only records: a
	// controller that starts watching a pawn which has already been hit — a
	// possession change, a rejoin — must not shut a panel because of a bullet
	// that landed before it was looking.
	const bool bFirstLook = LastSeenDamageSerial == INDEX_NONE;
	LastSeenDamageSerial = Serial;
	if (bFirstLook)
	{
		return;
	}

	if (Pawn.GetOpenContainerIndex() == INDEX_NONE)
	{
		return;
	}

	// Say so: the panel vanishing under the player's thumb is the one thing on
	// this HUD that happens without an input, and a log line is what makes it a
	// behaviour rather than a glitch someone reports.
	UE_LOG(LogTemp, Display,
		TEXT("SarkoPlayerController: hit while looting (damage serial %d) — closing the container panel"),
		Serial);
	Pawn.RequestCloseContainer();
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
		bEverLooted = true;
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
	//
	// `rifle` is fifth, right after the four-hue opening. It is the widest item
	// in the catalog at 3x1 and no loot table produces it, so without a place
	// here the only way to see a three-cell weapon in a grid would be to play a
	// version of the game that does not exist yet. A frame of the panel holding
	// one is what the weapon-mesh pass was checked against.
	static const FName Mixed[] = {
		TEXT("pistol"), TEXT("ammo_9mm"), TEXT("water_bottle"), TEXT("medkit"),
		TEXT("rifle"), TEXT("toolbox"), TEXT("canned_food"), TEXT("scrap_metal"),
		TEXT("shotgun"), TEXT("bandage"), TEXT("vodka"), TEXT("copper_wire"),
	};
	static const int32 Quantities[] = { 1, 47, 2, 3, 1, 1, 3, 9, 1, 5, 2, 12 };

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

void ASarkoPlayerController::SarkoDebugEquipWeapon(FString ItemId, float DelaySeconds, float ShotDelay)
{
#if !UE_BUILD_SHIPPING
	// -ExecCmds runs its whole list at engine init, and RestartPlayer runs
	// AFTER it — so an immediate equip has no pawn to equip, and even if it did
	// the spawn that follows would overwrite it with the profile's weapon. Same
	// deferral, and the same reasoning, as SarkoDebugSurvival below.
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, ItemId, ShotDelay]() { SarkoDebugEquipWeapon(ItemId, 0.f, ShotDelay); }),
			DelaySeconds, /*bLoop*/ false);
		return;
	}

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugEquipWeapon: no possessed pawn"));
		return;
	}
	Pawn->SetEquippedWeaponItem(FName(*ItemId));
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugEquipWeapon: now holding '%s'"), *ItemId);

	if (ShotDelay <= 0.f)
	{
		return;
	}
	// The shutter is chained off the EQUIP and not off engine start, for the
	// same reason SarkoTapContainerCell chains its own: the equip is on a timer
	// and a boot-relative shutter would catch it only by luck.
	//
	// HighResShot and NOT TakeDebugShot's `Shot showui`: this frame is about the
	// world, and `Shot` produced no file at all under -RenderOffscreen without a
	// window, where HighResShot's scene-renderer path works headless. The HUD is
	// not what is being photographed here.
	FTimerHandle ShotHandle;
	GetWorldTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(this,
		[this]() { ConsoleCommand(TEXT("HighResShot 1600x900"), /*bWriteToLog*/ true); }),
		ShotDelay, /*bLoop*/ false);
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

void ASarkoPlayerController::SarkoDebugSpawnBot(FString ArchetypeId, float X, float Y, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	// -ExecCmds runs its whole list at engine init, so a step that has to happen
	// LATER carries its own delay. Same shape as SarkoDebugSurvival above, and
	// rescheduled through this same function so the two paths cannot drift.
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, ArchetypeId, X, Y]() { SarkoDebugSpawnBot(ArchetypeId, X, Y, 0.f); }),
			DelaySeconds, /*bLoop*/ false);
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugSpawnBot: no world, or not the authority"));
		return;
	}

	const FVector Location(X, Y, 150.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASarkoEnemyCharacter* Enemy = World->SpawnActor<ASarkoEnemyCharacter>(
		ASarkoEnemyCharacter::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Enemy)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugSpawnBot: SpawnActor refused %s"), *Location.ToCompactString());
		return;
	}

	// The SAME call the encounter director makes, so the bot that appears here is
	// the bot a raid would have produced — archetype numbers, post and leash all
	// pushed through ApplyArchetypeAndPost rather than set by hand.
	Enemy->ApplyArchetypeAndPost(FName(*ArchetypeId), Location, /*LeashUU*/ 400.f);
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugSpawnBot: '%s' placed at %s, holding a 400 uu post"),
		*ArchetypeId, *Location.ToCompactString());
#endif
}

void ASarkoPlayerController::SarkoDebugMove(float DirX, float DirY, float Scale, float HoldSeconds, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, DirX, DirY, Scale, HoldSeconds]() { SarkoDebugMove(DirX, DirY, Scale, HoldSeconds, 0.f); }),
			DelaySeconds, /*bLoop*/ false);
		return;
	}

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugMove: no possessed pawn"));
		return;
	}

	// A HELD stick, not a single write. PlayerTick reassigns the move intent from
	// MoveStick every frame, so a one-shot SetMoveIntent here would be overwritten
	// before the pawn accelerated — ApplyDebugMoveInput below is applied on the
	// same tick and in the same place ApplyDesktopTestInput is, which is what
	// makes this a finger rather than a teleport.
	//
	// Scale IS the stick's deflection, so the velocity that results — and
	// therefore the noise the server classifies from it — is the one a thumb at
	// that deflection would produce. Nothing here touches the noise model.
	DebugMoveIntent = FVector2D(DirX, DirY).GetSafeNormal() * FMath::Clamp(Scale, 0.f, 1.f);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	DebugMoveUntilSeconds = Now + FMath::Max(0.f, HoldSeconds);
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugMove: stick held at %s (deflection %.2f) for %.1fs"),
		*DebugMoveIntent.ToString(), DebugMoveIntent.Size(), HoldSeconds);
#endif
}

#if !UE_BUILD_SHIPPING
bool ASarkoPlayerController::ApplyDebugMoveInput(ASarkoCharacter& Pawn)
{
	if (DebugMoveIntent.IsNearlyZero())
	{
		return false;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now >= DebugMoveUntilSeconds)
	{
		DebugMoveIntent = FVector2D::ZeroVector;
		Pawn.SetMoveIntent(FVector2D::ZeroVector);
		UE_LOG(LogTemp, Display,
			TEXT("SarkoDebugMove: stick released — the pawn is standing still, and standing still is silent"));
		return true;
	}

	Pawn.SetMoveIntent(DebugMoveIntent);
	return true;
}
#endif

void ASarkoPlayerController::SarkoDebugFire(float DirX, float DirY, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	if (DelaySeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
			[this, DirX, DirY]() { SarkoDebugFire(DirX, DirY, 0.f); }),
			DelaySeconds, /*bLoop*/ false);
		return;
	}

	ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetPawn());
	if (!Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugFire: no possessed pawn"));
		return;
	}

	// One shot, through the aim path a thumb would use: SetAimIntent then
	// RequestFire, so the direction is published, the server validates it and
	// USarkoWeaponComponent::ServerFire is what actually reports the noise.
	// CheatEmptyMagazine exists for the other question (does the magazine run
	// out); a noise verification wants exactly one bang.
	Pawn->SetAimIntent(FVector2D(DirX, DirY).GetSafeNormal(), /*bInIsAiming*/ true);
	Pawn->RequestFire();
	UE_LOG(LogTemp, Display, TEXT("SarkoDebugFire: one shot toward (%.2f, %.2f)"), DirX, DirY);
#endif
}
