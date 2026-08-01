#include "UI/SarkoHUD.h"

#include "CanvasItem.h"
#include "Combat/SarkoWeapon.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Engine/Canvas.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"
#include "Misc/ScopeExit.h"
#include "Pawn/SarkoCharacter.h"
#include "UI/SarkoInventoryPanel.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"

namespace
{
	/**
	 * Every size this HUD is drawn at, in **points** on the 844x390 landscape
	 * canvas described in UI/SarkoUiScale.h — i.e. what they measure on the glass
	 * of a phone, at any pixel density.
	 *
	 * The type sizes are chosen against the shelter menu, which was validated at
	 * real phone resolutions first: its title is 26 pt and its body 15 pt. The
	 * clock and the ammo count are the two things a player glances at mid-fight
	 * without stopping, so they get title-sized type; everything the player only
	 * reads while standing still is nearer body size.
	 */
	constexpr float ClockPt = 26.f;
	constexpr float AmmoPt = 26.f;
	constexpr float BackpackPt = 20.f;
	constexpr float ConnectingPt = 15.f;
	constexpr float PromptPt = 17.f;
	constexpr float ExtractPt = 20.f;
	constexpr float DiedPt = 40.f;
	constexpr float OutcomeTitlePt = 46.f;
	constexpr float ReturningPt = 17.f;
	/** The interact button's label at 12 pt: ОБШУКАТИ is 8 Cyrillic capitals,
	 *  ~67 pt wide inside a 96 pt button. The reload count is 20 pt, centred. */
	constexpr float InteractLabelPt = 12.f;
	constexpr float ReloadLabelPt = 20.f;

	/** In from the safe frame's side edges, and down from its top. */
	constexpr float SideInsetPt = 16.f;
	constexpr float TopRowPt = 10.f;

	/**
	 * The top rows, stacked down the safe frame in points.
	 *
	 * The order is the old one — clock and readouts, then the connection warning,
	 * then the loot prompt, then the extraction banner — and the gaps are sized so
	 * that any two of them showing at once (holding a crate inside an extraction
	 * zone is a real state) still read as separate lines.
	 */
	constexpr float ConnectingTopPt = 46.f;
	constexpr float PromptTopPt = 72.f;
	constexpr float ExtractTopPt = 118.f;

	/** The health bar, top-right, vertically inside the clock's row. */
	constexpr float HealthBarWidthPt = 150.f;
	constexpr float HealthBarHeightPt = 11.f;
	constexpr float HealthBarTopPt = 16.f;

	/** The loot channel's progress bar, under the prompt it belongs to. */
	constexpr float LootBarWidthPt = 170.f;
	constexpr float LootBarHeightPt = 8.f;
	constexpr float LootBarGapPt = 8.f;

	/** Padding inside the dark plate behind a piece of text. */
	constexpr float PlatePadXPt = 10.f;
	constexpr float PlatePadYPt = 4.f;

	/** Stroke weights. Hairlines at 1:1 disappear on a phone. */
	constexpr float StickStrokePt = 1.5f;
	constexpr float AimConeStrokePt = 1.f;
	constexpr float StickDotPt = 11.f;

	/** The drop shadow every readout gets. The HUD is drawn over an arbitrary
	 *  world, and white-on-white is the one failure that no size fixes. */
	const FLinearColor TextShadow(0.f, 0.f, 0.f, 0.75f);

	/** The backing under the two thumb-column buttons, for the same reason: a
	 *  translucent tint alone disappears against pale ground, and a control the
	 *  player cannot find is worse than one they do not like the look of. */
	const FLinearColor ThumbButtonPlate(0.f, 0.f, 0.f, 0.45f);
}

void ASarkoHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	// UCanvas applies the platform's safe zone itself before PostRender: on a
	// device that reports insets it translates the canvas origin inward and
	// shrinks SizeX/SizeY to match (Engine's UCanvas::ApplySafeZoneTransform).
	// That is the same job SarkoInput::SafeFrame does, and doing both insets the
	// HUD twice — while ASarkoPlayerController::UpdateSticks hit-tests the
	// interact button in raw viewport pixels, having done it once. The button
	// would then be drawn in one place and pressed in another, which is precisely
	// the bug this HUD had before and the one the player experiences as "it does
	// not work". So the engine's transform is lifted for the duration of our own
	// drawing and put back afterwards, leaving exactly one authority on the safe
	// area — SafeFrame, the one the input path also uses.
	//
	// Both calls return immediately when the platform reports no insets, which is
	// every desktop and every headless run, so nothing about the Mac changes.
	Canvas->PopSafeZoneTransform();
	ON_SCOPE_EXIT{ Canvas->ApplySafeZoneTransform(); };

	// Once per frame, before anything is placed. See the members' comments.
	const FVector2D ViewportSize(Canvas->SizeX, Canvas->SizeY);
	Safe = SarkoInput::SafeFrame(ViewportSize);

	const float NewScale = SarkoUI::PointScaleForViewport(ViewportSize);
	if (NewScale != MeasuredAtScale)
	{
		PointScale = NewScale;
		InvalidateMeasurements();

		// Logged on the scale changing rather than per frame — so once, on the
		// first frame of a raid, and again only if the window is resized. The whole
		// layout is expressed in points and this factor is the only thing turning
		// it into pixels; a wrong one is an unreadable HUD and nothing else says so.
		const FVector2D Digits = MeasurePt(TEXT("00:00"), ClockPt);
		UE_LOG(LogTemp, Display,
			TEXT("SarkoHUD: viewport %.0fx%.0f px, scale %.3f px/pt (canvas %.0fx%.0f pt); ")
			TEXT("clock %.0f px = %.1f pt tall, interact button %.0f px = %.1f pt"),
			ViewportSize.X, ViewportSize.Y, PointScale,
			ViewportSize.X / PointScale, ViewportSize.Y / PointScale,
			Digits.Y, Digits.Y / PointScale,
			SarkoInput::InteractButtonRect(Safe, PointScale).GetSize().X,
			SarkoInput::InteractButtonRect(Safe, PointScale).GetSize().X / PointScale);

		MeasuredAtScale = NewScale;
	}

	DrawStick(PC->GetMoveStick(), FLinearColor(1.f, 1.f, 1.f, 0.35f));
	DrawStick(PC->GetAimStick(), FLinearColor(1.f, 0.85f, 0.2f, 0.45f));
	DrawAimCone();
	DrawTopBar();
	DrawHealth();
	DrawAmmo();
	DrawBackpack();
	DrawInteract();
	DrawReload();
	DrawExtraction();
	// Last, so the final screen is over everything else rather than under it.
	DrawOutcomeSummary();
}

void ASarkoHUD::InvalidateMeasurements()
{
	CachedClockSeconds = -1;
	CachedReloadingWidth = -1.f;
	CachedInteractLabelWidth = -1.f;
	CachedInteractLabel.Reset();
	CachedReloadLabel.Reset();
	bPromptCached = false;
}

FSlateFontInfo ASarkoHUD::FontPt(float PointSize) const
{
	// The engine's large font, asked for at a size rather than magnified from one.
	// With no LargeFontName configured it resolves to a transient UFont whose
	// RuntimeFontSource is CoreStyleDefault — so this is FCoreStyle's face, the
	// same one the shelter menu draws, and still no font asset.
	//
	// Not FCoreStyle::GetDefaultFontStyle() directly, which is what the shelter
	// calls: the style hands back an FSlateFontInfo with a null FontObject, and
	// FCanvasSimpleTextItem::HasValidText() tests exactly that pointer and then
	// silently draws nothing. A HUD that renders every bar and no text at all is
	// how that reads from the outside.
	UFont* Large = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Large)
	{
		return FSlateFontInfo();
	}
	// Runtime-cached, so overriding the size asks the font cache to rasterise
	// glyphs at that size instead of scaling up the 10-pixel ones the legacy size
	// would give — which is the difference between a readable clock and a blurry
	// one at the 3x a phone needs.
	FSlateFontInfo Info = Large->GetLegacySlateFontInfo();
	Info.Size = FMath::Max(1.f, PointSize * PointScale);
	return Info;
}

FVector2D ASarkoHUD::MeasurePt(const FString& Text, float PointSize) const
{
	if (!FEngineFontServices::IsInitialized())
	{
		return FVector2D::ZeroVector;
	}
	const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::Get().GetFontMeasure();
	if (!Measure.IsValid())
	{
		return FVector2D::ZeroVector;
	}
	// FontScale 1, matching the canvas: the size asked for in FontPt is already in
	// pixels, and the canvas the HUD draws into has a DPI scale of exactly 1.
	return Measure->Measure(Text, FontPt(PointSize), 1.f);
}

void ASarkoHUD::DrawTextPt(const FString& Text, const FLinearColor& Colour, float X, float Y, float PointSize)
{
	// FStringView and not FText: DrawHUD is a tick path and the strings handed to
	// this are the cached ones the class already keeps, so nothing here allocates.
	FCanvasTextStringViewItem Item(FVector2D(X, Y), Text, FontPt(PointSize), Colour);
	Item.EnableShadow(TextShadow, FVector2D(FMath::Max(1.f, Px(1.f)), FMath::Max(1.f, Px(1.f))));
	Canvas->DrawItem(Item);
}

void ASarkoHUD::DrawStick(const FSarkoTouchStick& Stick, const FLinearColor& Colour)
{
	if (!Stick.bActive)
	{
		return;
	}

	// Ring at the thumb's landing point, dot at the current position.
	//
	// The ring's radius is the one thing on this HUD that is deliberately *not*
	// scaled: FSarkoTouchStick::RadiusPx is the screen distance at which the stick
	// reads full deflection, so the ring is a picture of the input rule and would
	// be lying if it were drawn at any other size. Only the ink is scaled — a
	// two-pixel stroke is invisible at 3x, which is how a stick could be active
	// and look like it was not.
	const int32 Segments = 24;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A0 = (2.f * PI * i) / Segments;
		const float A1 = (2.f * PI * (i + 1)) / Segments;
		DrawLine(
			Stick.Origin.X + FMath::Cos(A0) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.Y + FMath::Sin(A0) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.X + FMath::Cos(A1) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.Y + FMath::Sin(A1) * FSarkoTouchStick::RadiusPx,
			Colour, Px(StickStrokePt));
	}
	const float Dot = Px(StickDotPt);
	DrawRect(Colour, Stick.Current.X - Dot * 0.5f, Stick.Current.Y - Dot * 0.5f, Dot, Dot);
}

void ASarkoHUD::DrawAimCone()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->IsAiming())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FVector Muzzle = Pawn->GetMuzzleLocation();
	const FVector Aim = FVector(Pawn->AimDirection);

	// Two edges of the cone, projected to screen. On a touchscreen there is no
	// cursor, so without this the player is shooting blind.
	const auto ProjectAndDraw = [this, &Muzzle](const FVector& End, const FLinearColor& Colour)
	{
		const FVector Start2D = Project(Muzzle);
		const FVector End2D = Project(End);
		DrawLine(Start2D.X, Start2D.Y, End2D.X, End2D.Y, Colour, Px(AimConeStrokePt));
	};

	const FVector Left = Muzzle + Aim.RotateAngleAxis(Settings.AimConeHalfAngleDegrees, FVector::UpVector) * Settings.WeaponRangeUU;
	const FVector Right = Muzzle + Aim.RotateAngleAxis(-Settings.AimConeHalfAngleDegrees, FVector::UpVector) * Settings.WeaponRangeUU;

	const FLinearColor Colour(1.f, 0.85f, 0.2f, 0.5f);
	ProjectAndDraw(Left, Colour);
	ProjectAndDraw(Right, Colour);
}

void ASarkoHUD::DrawTopBar()
{
	// Top only: the lower corners are dead zones under the player's thumbs.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState)
	{
		return;
	}

	// Rebuilt on the second, not on the frame: the string only ever changes when
	// the integer second does, and DrawHUD is a tick path where a Printf and a
	// GetTextSize per frame is 59 of every 60 rebuilds discarded.
	const int32 Total = FMath::CeilToInt(RaidState->RemainingSeconds);
	if (Total != CachedClockSeconds)
	{
		CachedClockSeconds = Total;
		CachedClock = FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
		CachedClockWidth = MeasurePt(CachedClock, ClockPt).X;
	}

	DrawTextPt(CachedClock, FLinearColor::White,
		Safe.GetCenter().X - CachedClockWidth * 0.5f, Safe.Min.Y + Px(TopRowPt), ClockPt);

	// The player must be able to tell "the raid has not started yet" from "the
	// crates are broken". Spec §4.6's loud degradation is a log line for the
	// developer; this is the same fact for the player. Rebuilt per frame rather
	// than cached, unlike the clock: it is on screen for one HTTP round trip, so
	// there is no steady state worth optimising.
	if (!RaidState->bSessionReady)
	{
		static const FString Connecting(TEXT("З'ЄДНАННЯ..."));
		const float Width = MeasurePt(Connecting, ConnectingPt).X;
		DrawTextPt(Connecting, FLinearColor(1.f, 0.75f, 0.2f),
			Safe.GetCenter().X - Width * 0.5f, Safe.Min.Y + Px(ConnectingTopPt), ConnectingPt);
	}
}

void ASarkoHUD::DrawHealth()
{
	// Health was missing entirely, and its absence produced the worst kind of
	// bug report: "the character walks, then stops walking when enemies show
	// up". The character had died — death disables movement — and nothing on
	// screen said so. A player must always be able to see that they are dying,
	// and that they are dead.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->HealthComponent)
	{
		return;
	}

	const USarkoHealthComponent* Health = Pawn->HealthComponent;
	const float Fraction = Health->GetMaxHealth() > 0.f
		? FMath::Clamp(Health->GetHealth() / Health->GetMaxHealth(), 0.f, 1.f)
		: 0.f;

	const float BarWidth = Px(HealthBarWidthPt);
	const float BarHeight = Px(HealthBarHeightPt);
	// Measured from the safe frame's right edge, not the canvas': in landscape
	// that edge is 59 pt in from the glass on a notched phone.
	const float BarX = Safe.Max.X - BarWidth - Px(SideInsetPt);
	const float BarY = Safe.Min.Y + Px(HealthBarTopPt);
	const float Border = Px(2.f);

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f),
		BarX - Border, BarY - Border, BarWidth + Border * 2.f, BarHeight + Border * 2.f);

	// Green through red, so falling health is readable without reading a number.
	const FLinearColor Fill = FMath::Lerp(FLinearColor(0.85f, 0.15f, 0.1f), FLinearColor(0.3f, 0.85f, 0.25f), Fraction);
	DrawRect(Fill, BarX, BarY, BarWidth * Fraction, BarHeight);

	if (!Health->IsDead())
	{
		return;
	}

	// Once the raid has an outcome, DrawOutcomeSummary says KIA in the same place
	// and says it better. Two centred death banners stacked on each other is
	// worse than either alone.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && RaidState->IsRaidFinished())
	{
		return;
	}

	// Unmissable, centred: this is the state the tester could not previously see.
	static const FString DeadText(TEXT("YOU DIED"));
	const float TextWidth = MeasurePt(DeadText, DiedPt).X;
	DrawTextPt(DeadText, FLinearColor(1.f, 0.2f, 0.15f),
		Safe.GetCenter().X - TextWidth * 0.5f, Safe.Min.Y + Safe.GetSize().Y * 0.42f, DiedPt);
}

void ASarkoHUD::DrawAmmo()
{
	// Top-left, alongside the clock: still along the top per spec §9, clear of
	// both bottom corners so the sticks never cover it. Reloading is drawn as
	// distinct text rather than a number so the tester can tell "reloading"
	// from "empty and stuck" at a glance — the whole point of this readout.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		return;
	}

	const USarkoWeaponComponent* Weapon = Pawn->WeaponComponent;
	const bool bReloading = Weapon->IsReloading();

	// Rebuilt when the readout changes, which is on a shot or a reload rather than
	// on a frame. FromInt allocates, and so does turning the TEXT("RELOADING")
	// literal into the FString DrawText takes — both were paid every frame for a
	// string with two digits' worth of variation.
	const int32 AmmoKey = bReloading ? INDEX_NONE : Weapon->GetAmmoInMagazine();
	if (AmmoKey != CachedAmmoKey)
	{
		CachedAmmoKey = AmmoKey;
		CachedAmmoText = bReloading ? FString(TEXT("RELOADING")) : FString::FromInt(AmmoKey);
	}

	const FLinearColor Colour = bReloading ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;

	DrawTextPt(CachedAmmoText, Colour, Safe.Min.X + Px(SideInsetPt), Safe.Min.Y + Px(TopRowPt), AmmoPt);
}

void ASarkoHUD::DrawBackpack()
{
	// Top-left, immediately right of the ammo readout: spec §9 puts every
	// number along the top, and the bottom corners belong to the thumbs.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->BackpackComponent)
	{
		return;
	}

	const USarkoBackpackComponent* Backpack = Pawn->BackpackComponent;
	// Cells, not stacks: with sizes a toolbox costs two, and a readout that said
	// 1/12 for half a full pocket row would be lying about the only number the
	// player uses to decide whether to keep looting.
	const int32 Used = Backpack->GetUsedCells();
	const int32 Limit = Backpack->GetCellCount();

	// Rebuilt when a slot is taken or the limit changes — a few times a raid —
	// rather than every frame. Both halves are the key because the limit comes from
	// a setting, not a constant.
	if (Used != CachedBackpackUsed || Limit != CachedBackpackLimit)
	{
		CachedBackpackUsed = Used;
		CachedBackpackLimit = Limit;
		CachedBackpackText = FString::Printf(TEXT("%d/%d"), Used, Limit);
	}

	// The x offset is measured from the widest string DrawAmmo can produce
	// rather than guessed: "RELOADING" in the large font is far wider than the
	// two digits of a magazine count, and a fixed offset that clears "30"
	// overlaps it the moment the player reloads.
	//
	// Measured once per scale and kept. DrawHUD is a tick path, and measuring
	// inline allocated and freed a string every frame to re-derive a constant.
	if (CachedReloadingWidth < 0.f)
	{
		static const FString Reloading(TEXT("RELOADING"));
		CachedReloadingWidth = MeasurePt(Reloading, AmmoPt).X;
	}
	const float X = Safe.Min.X + Px(SideInsetPt) + CachedReloadingWidth + Px(SideInsetPt);

	// Amber when full, so "the crate had more in it" is legible at a glance
	// rather than being discovered by counting.
	const FLinearColor Colour = Used >= Limit ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;
	// Baseline-ish alignment with the taller ammo readout beside it: sitting both
	// at the same top edge would leave the smaller number floating.
	DrawTextPt(CachedBackpackText, Colour, X, Safe.Min.Y + Px(TopRowPt + (AmmoPt - BackpackPt) * 0.75f), BackpackPt);
}

void ASarkoHUD::DrawInteract()
{
	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	// The button is always drawn, so the player learns where it is before they
	// need it; it dims when there is nothing in reach.
	//
	// The rect comes from SarkoInput::InteractButtonRect and is *not* scaled here:
	// it is already derived from the safe frame. Scaling it again would make the
	// drawn button a different rectangle from the one
	// ASarkoPlayerController::UpdateSticks hit-tests, which is the one thing about
	// this button that must never happen — the owner experiences that as "the
	// button doesn't work".
	//
	// It never shifts. There used to be a second rect, beside an open container
	// panel, and a shared function to choose between them; the panel moved to the
	// left half (spec §4.5), so the thing the shift was avoiding is not there any
	// more and both the shifted rect and the chooser are deleted. This note is
	// deliberately not deleted with them: a stale comment about the branch is how
	// the branch comes back.
	const ASarkoCharacter* OwningPawn = Cast<ASarkoCharacter>(GetOwningPawn());
	const FBox2D Rect = SarkoInput::InteractButtonRect(Safe, PointScale);

	// The label says what the button will DO (spec §4.4). A generic glyph in a
	// game with two actions is a guess, and the button is always drawn — dim and
	// BLANK when there is nothing in reach — so the player learns where it is
	// before they need it and the empty state is honest about there being
	// nothing to do.
	const ASarkoLootContainer* Target = PC->GetInteractTarget();
	const bool bPanelOpen = OwningPawn && OwningPawn->GetOpenContainerIndex() != INDEX_NONE;
	const SarkoUI::EInteractAction Action = bPanelOpen
		? SarkoUI::EInteractAction::Close
		: (Target ? SarkoUI::EInteractAction::Search : SarkoUI::EInteractAction::None);

	// A dark plate under the tint, exactly as every readout on this HUD has one:
	// the button is drawn over an arbitrary world, and a 0.15-alpha white plate on
	// pale ground is a control the player cannot find. See TextShadow's comment —
	// white-on-white is the one failure that no size fixes.
	DrawRect(ThumbButtonPlate, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);
	const FLinearColor ButtonColour = (Action == SarkoUI::EInteractAction::None)
		? FLinearColor(1.f, 1.f, 1.f, 0.15f)
		: FLinearColor(0.95f, 0.8f, 0.25f, 0.55f);
	DrawRect(ButtonColour, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);

	const FString Label = SarkoUI::InteractLabelFor(Action);
	if (!Label.IsEmpty())
	{
		// Keyed on the STRING, not on the action: two actions can share a width
		// and none can share a string. Measured on a change rather than per frame,
		// because DrawHUD is a tick path.
		if (Label != CachedInteractLabel || CachedInteractLabelWidth < 0.f)
		{
			CachedInteractLabel = Label;
			const FVector2D LabelSize = MeasurePt(Label, InteractLabelPt);
			CachedInteractLabelWidth = LabelSize.X;
			CachedInteractLabelHeight = LabelSize.Y;
		}
		DrawTextPt(CachedInteractLabel, FLinearColor::White,
			Rect.GetCenter().X - CachedInteractLabelWidth * 0.5f,
			Rect.GetCenter().Y - CachedInteractLabelHeight * 0.5f,
			InteractLabelPt);
	}

	// A container panel is up: this control is the CLOSE button now, and the
	// prompt and the channel bar are skipped outright — neither means anything
	// while a panel is open, and the prompt would sit under the clock saying
	// "search" about a crate the player is already inside.
	if (bPanelOpen)
	{
		return;
	}

	if (!Target)
	{
		return;
	}

	// Prompt: top-centre, under the clock. Never a bottom corner (spec §9).
	//
	// Built and measured only when the tier changes, which is the only thing the
	// text depends on. Doing it inline cost a Printf, an FName::ToString and a
	// GetTextSize every frame the player stood near a crate.
	if (!bPromptCached || Target->Tier != CachedPromptTier)
	{
		bPromptCached = true;
		CachedPromptTier = Target->Tier;
		CachedPrompt = FString::Printf(TEXT("ОБШУКАТИ (%s)"), *CachedPromptTier.ToString());
		const FVector2D PromptSize = MeasurePt(CachedPrompt, PromptPt);
		CachedPromptWidth = PromptSize.X;
		CachedPromptHeight = PromptSize.Y;
	}

	const float PromptWidth = CachedPromptWidth;
	const float PromptHeight = CachedPromptHeight;
	const float PromptX = Safe.GetCenter().X - PromptWidth * 0.5f;
	const float PromptY = Safe.Min.Y + Px(PromptTopPt);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f),
		PromptX - Px(PlatePadXPt), PromptY - Px(PlatePadYPt),
		PromptWidth + Px(PlatePadXPt * 2.f), PromptHeight + Px(PlatePadYPt * 2.f));
	DrawTextPt(CachedPrompt, FLinearColor::White, PromptX, PromptY, PromptPt);

	if (!PC->IsInteractHeld())
	{
		return;
	}

	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	// Progress: local and cosmetic. The server owns whether the channel
	// completes; this bar only has to stop the player wondering whether the
	// hold is doing anything.
	const float Duration = FMath::Max(0.01f, GetDefault<USarkoRaidSettings>()->LootChannelSeconds);
	const float Fraction = FMath::Clamp(Pawn->GetLootChannelElapsed() / Duration, 0.f, 1.f);

	const float BarWidth = Px(LootBarWidthPt);
	const float BarHeight = Px(LootBarHeightPt);
	const float BarX = Safe.GetCenter().X - BarWidth * 0.5f;
	const float BarY = PromptY + PromptHeight + Px(LootBarGapPt);
	const float Border = Px(2.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f),
		BarX - Border, BarY - Border, BarWidth + Border * 2.f, BarHeight + Border * 2.f);
	DrawRect(FLinearColor(0.95f, 0.8f, 0.25f, 0.9f), BarX, BarY, BarWidth * Fraction, BarHeight);
}

void ASarkoHUD::DrawReload()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		return;
	}

	// Always drawn, in a rect that takes no game state: the interact button
	// appearing above it cannot shift it, because both are computed from the safe
	// frame alone. A control that moves is a control you mis-press (spec §5).
	const FBox2D Rect = SarkoInput::ReloadButtonRect(Safe, PointScale);

	const USarkoWeaponComponent* Weapon = Pawn->WeaponComponent;
	const int32 Magazine = GetDefault<USarkoRaidSettings>()->MagazineSize;
	const SarkoUI::ESarkoReloadState State =
		SarkoUI::ReloadStateFor(Weapon->GetAmmoInMagazine(), Magazine, Weapon->IsReloading());

	// Spec §4.3: "the magazine count lives on it, it goes amber below a third, and
	// it pulses when empty. The player should never have to look at two places to
	// know they need to reload."
	FLinearColor Fill;
	FLinearColor Ink;
	FString Label;
	switch (State)
	{
	case SarkoUI::ESarkoReloadState::Low:
		Fill = FLinearColor(0.95f, 0.55f, 0.06f, 0.35f);
		Ink = FLinearColor(1.f, 0.6f, 0.1f, 1.f);
		Label = FString::FromInt(Weapon->GetAmmoInMagazine());
		break;
	case SarkoUI::ESarkoReloadState::Empty:
		// The pulse is the one animated thing on this HUD, and it is bounded away
		// from zero: a button that vanishes on the trough reads as absent, not as
		// urgent.
		Fill = FLinearColor(0.95f, 0.55f, 0.06f,
			SarkoUI::ReloadPulseAlpha(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f));
		Ink = FLinearColor(1.f, 0.6f, 0.1f, 1.f);
		Label = TEXT("0");
		break;
	case SarkoUI::ESarkoReloadState::Reloading:
		Fill = FLinearColor(1.f, 1.f, 1.f, 0.10f);
		Ink = FLinearColor(0.7f, 0.7f, 0.7f, 1.f);
		Label = TEXT("…");
		break;
	default:
		Fill = FLinearColor(1.f, 1.f, 1.f, 0.15f);
		Ink = FLinearColor::White;
		Label = FString::FromInt(Weapon->GetAmmoInMagazine());
		break;
	}

	// Dark plate first, state tint over it: see DrawInteract for why.
	DrawRect(ThumbButtonPlate, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);
	DrawRect(Fill, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);

	// Measured when the string changes — on a shot or a reload — rather than every
	// frame. DrawHUD is a tick path and an uncached MeasurePt per frame is the
	// allocation this HUD spent two commits removing.
	if (Label != CachedReloadLabel)
	{
		CachedReloadLabel = Label;
		const FVector2D Size = MeasurePt(Label, ReloadLabelPt);
		CachedReloadLabelWidth = Size.X;
		CachedReloadLabelHeight = Size.Y;
	}
	DrawTextPt(CachedReloadLabel, Ink,
		Rect.GetCenter().X - CachedReloadLabelWidth * 0.5f,
		Rect.GetCenter().Y - CachedReloadLabelHeight * 0.5f,
		ReloadLabelPt);
}

const FString& ASarkoHUD::ZoneNameFor(int32 ZoneIndex)
{
	// Fallback, not a label: a generic word here means the map file and the
	// server's zone list have drifted, which should be visible rather than
	// crash the HUD.
	static const FString Generic(TEXT("ЕВАКУАЦІЯ"));

	if (!bZoneNamesCached)
	{
		// Once per HUD, not once per frame. DrawHUD is a tick path.
		bZoneNamesCached = true;
		FSarkoMapDefinition Definition;
		FString Error;
		if (SarkoMap::LoadDefinitionFromDisk(GetDefault<USarkoRaidSettings>()->MapId.ToString(), Definition, Error))
		{
			CachedZoneNames.Reserve(Definition.Extractions.Num());
			for (const FSarkoExtractionSpot& Spot : Definition.Extractions)
			{
				CachedZoneNames.Add(Spot.Name.IsEmpty() ? Generic : Spot.Name);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SarkoHUD: extraction zone names unavailable: %s"), *Error);
		}
	}

	return CachedZoneNames.IsValidIndex(ZoneIndex) ? CachedZoneNames[ZoneIndex] : Generic;
}

void ASarkoHUD::DrawExtraction()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || Pawn->ExtractZoneIndex == INDEX_NONE)
	{
		return;
	}

	const float Required = FMath::Max(0.1f, GetDefault<USarkoRaidSettings>()->ExtractDwellSeconds);
	const float Left = FMath::Max(0.f, Required - Pawn->ExtractDwellSeconds);
	const FString Text = FString::Printf(TEXT("%s — %.1f"), *ZoneNameFor(Pawn->ExtractZoneIndex), Left);

	// Top-centre, below the clock and the loot prompt's slot: everything
	// informational lives along the top (spec §9), and never a bottom corner.
	const FVector2D Size = MeasurePt(Text, ExtractPt);
	const float X = Safe.GetCenter().X - Size.X * 0.5f;
	const float Y = Safe.Min.Y + Px(ExtractTopPt);
	DrawRect(FLinearColor(0.f, 0.25f, 0.05f, 0.55f),
		X - Px(PlatePadXPt * 1.4f), Y - Px(PlatePadYPt * 1.5f),
		Size.X + Px(PlatePadXPt * 2.8f), Size.Y + Px(PlatePadYPt * 3.f));
	DrawTextPt(Text, FLinearColor(0.55f, 1.f, 0.6f), X, Y, ExtractPt);
}

void ASarkoHUD::DrawOutcomeSummary()
{
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState || !RaidState->IsRaidFinished())
	{
		return;
	}

	FString Title;
	FLinearColor Colour;
	switch (RaidState->Outcome)
	{
	case ESarkoRaidOutcome::Extracted: Title = TEXT("EXTRACTED"); Colour = FLinearColor(0.4f, 1.f, 0.45f); break;
	case ESarkoRaidOutcome::MIA:       Title = TEXT("MIA");       Colour = FLinearColor(1.f, 0.65f, 0.2f);  break;
	default:                           Title = TEXT("KIA");       Colour = FLinearColor(1.f, 0.25f, 0.2f);  break;
	}

	// Dim the world so the summary is unmistakably a final screen rather than
	// another HUD element. The whole canvas, deliberately: the dim is the only
	// thing here that should reach under a notch, because a lit strip along the
	// edge of a dimmed screen reads as a rendering fault.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	const FVector2D TitleSize = MeasurePt(Title, OutcomeTitlePt);
	float Y = Safe.Min.Y + Safe.GetSize().Y * 0.22f;
	DrawTextPt(Title, Colour, Safe.GetCenter().X - TitleSize.X * 0.5f, Y, OutcomeTitlePt);
	Y += TitleSize.Y + Px(16.f);

	// The itemised haul used to be drawn here and now lives in the shelter (spec
	// §6.5: "вынесено: …" moves there, where it sits above the stash it was just
	// credited into and can be compared with it). What stays is the outcome itself,
	// in the place the raid ended, plus a line saying where the player is about to
	// go — without it, PostRaidReturnSeconds of a dimmed frozen world reads as a
	// hang rather than as a beat.
	//
	// Keyed on bReturningToShelter, not on the outcome: ASarkoRaidGameMode::
	// ReturnToShelter returns without scheduling anything when there is no
	// USarkoGameInstance, and then the dimmed world really is where the player
	// stays. Promising a return that is not coming is the one thing this line must
	// not do, so the stuck case says so instead — the Error log explains it, and
	// this tells the player to go look.
	const FString Returning = RaidState->bReturningToShelter
		? TEXT("ПОВЕРНЕННЯ ДО УКРИТТЯ...")
		: TEXT("ПОВЕРНЕННЯ НЕДОСТУПНЕ — ДИВ. ЛОГ");
	const float Width = MeasurePt(Returning, ReturningPt).X;
	const FLinearColor ReturningColour = RaidState->bReturningToShelter
		? FLinearColor(0.8f, 0.8f, 0.8f)
		: FLinearColor(1.f, 0.65f, 0.2f);
	DrawTextPt(Returning, ReturningColour, Safe.GetCenter().X - Width * 0.5f, Y, ReturningPt);
}
