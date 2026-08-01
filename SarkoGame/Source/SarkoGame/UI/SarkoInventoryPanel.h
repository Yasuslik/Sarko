#pragma once

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "Types/SlateStructs.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

// By value in PreviousBag and by const-ref in BuildCell, so the complete
// USTRUCT has to be visible here rather than forward-declared.
#include "Loot/SarkoItemCatalog.h"

class ASarkoCharacter;
class SBorder;
class SButton;
class SHorizontalBox;
class STextBlock;
class SVerticalBox;
struct FSarkoInventoryStyles;
enum class ESarkoTakeRefusal : uint8;

namespace SarkoUI
{
	/** Every size below is in points on the 844x390 landscape canvas
	 *  (SarkoUiScale.h), i.e. what it measures on the glass at any density. */
	constexpr float CellSizePt = 44.f;        // == the tap-target minimum, exactly
	constexpr float CellGutterPt = 4.f;       // 48 pt slot pitch
	constexpr float CellPadPt = 4.f;          // inside a cell, around its label
	constexpr float PanelPadPt = 14.f;        // left and right
	/**
	 * Top and bottom. Twelve and not fourteen, because the Visual design's
	 * vertical stack adds up to the 292 pt the panel is specified at only with
	 * twelve (12 + 44 + 6 + 44 + 12 + 16 + 6 + 140 + 12), and the height is the
	 * axis that decides whether a full panel clears the health bar. The
	 * horizontal padding stays 14, which is what makes the width exactly 216.
	 */
	constexpr float PanelPadYPt = 12.f;
	constexpr int32 GridColumns = 4;
	constexpr float PanelWidthPt = CellSizePt * GridColumns + CellGutterPt * (GridColumns - 1) + PanelPadPt * 2.f;  // 216
	constexpr float TakeAllRowPt = 44.f;
	constexpr float HeaderRowPt = 16.f;
	constexpr float DividerPt = 12.f;
	constexpr float GridGapPt = 6.f;

	/** Type sizes, from the Visual design table. */
	constexpr float SectionHeaderPt = 11.f;
	constexpr float TakeAllPt = 12.f;
	constexpr float CellCountPt = 10.f;

	/**
	 * 7.5 and not the table's 8.5, settled by reading the frame rather than the
	 * spec: at 8.5 a 36 pt cell interior held four Cyrillic capitals before the
	 * ellipsis, so ПІСТОЛЕТ and ПАТРОНИ both came out "П..." and the two junk
	 * items were indistinguishable from each other. At 7.5 it holds six, which is
	 * enough to tell two items of the SAME hue apart — the only job the label has,
	 * since the hue already carries the category.
	 */
	constexpr float CellLabelPt = 7.5f;

	/**
	 * The crate's tier is the header's headline and "ОБШУК" is its dim label
	 * above it, stacked rather than run together on one line: at 216 pt wide,
	 * "ОБШУК · MILITARY" and "ЗАБРАТИ ВСЕ" on one row overlapped each other, and
	 * a header printed through a button label is the first thing that makes a
	 * screen look unfinished.
	 */
	constexpr float SectionLabelPt = 9.f;
	constexpr float TierPt = 13.f;

	/** How far the panel slides in from the right on entry. */
	constexpr float EntrySlidePt = 24.f;

	/** In from the safe frame's right edge, and up from its bottom. The bottom
	 *  number keeps the panel clear of the home indicator AND of the aim thumb's
	 *  resting corner, in that order of importance. */
	constexpr float PanelRightInsetPt = 16.f;
	constexpr float PanelBottomInsetPt = 20.f;

	/** Rows of four the player's cells occupy. At least one, so an impossible
	 *  zero-capacity pawn still draws a panel rather than a sliver. */
	int32 PlayerGridRows(int32 PlayerCells);

	/** The vertical stack, added up: 12 + 44 + 6 + 44 + 12 + 16 + 6 + grid + 12.
	 *  Shared with the widget rather than re-derived there, so the rect the HUD
	 *  positions the close button against and the box Slate actually lays out
	 *  cannot disagree. */
	float InventoryPanelHeightPt(int32 PlayerCells);

	/**
	 * Where the panel goes, in whatever unit SafeFrame is in (pixels for the HUD,
	 * points for the widget — PointScale is the bridge). Pure, so the one property
	 * that decides whether a player can see the bot walking at them is unit tested
	 * without a viewport, a widget or a Slate application.
	 *
	 * Bottom-anchored and growing UPWARD, which is what keeps a full 12-cell
	 * panel clear of the HUD's health bar (y 14..29) while a 4-cell one sits
	 * lower still. Top-anchoring put the header 3 points under the bar, which
	 * reads as a collision rather than as a layout.
	 */
	FBox2D InventoryPanelRect(FBox2D SafeFrame, int32 PlayerCells, float PointScale);

	/**
	 * Where the interact button is for this pawn right now, in viewport pixels:
	 * its usual place, or beside the panel while one is open.
	 *
	 * One function and not two branches in two files, because the HUD draws that
	 * button and the controller hit-tests it, and a button drawn in one place and
	 * pressed in another is the one thing about this control that must never
	 * happen. A null pawn, or one with nothing open, gets the ordinary rect.
	 */
	FBox2D InteractButtonRectFor(const ASarkoCharacter* Pawn, FBox2D SafeFrame, float PointScale);

	/** +-4 pt over two full cycles, starting and ending at exactly zero. Pure, so
	 *  the "ends at rest" property is a test rather than an eyeball. */
	inline float RefusalShakeOffsetPt(float Lerp)
	{
		return 4.f * FMath::Sin(4.f * PI * FMath::Clamp(Lerp, 0.f, 1.f));
	}
}

/**
 * The container panel: what is in the crate, what is in your bag, and one tap
 * between them.
 *
 * Bottom-right quarter of the safe frame and translucent, because looting does
 * not pause the world (spec §2.4/§5): the pawn at screen centre and 190 points
 * of ground to its right are never covered, and a bot crossing behind the plate
 * stays a moving silhouette rather than disappearing behind a dim.
 *
 * **The controller must NOT set FInputModeUIOnly for this panel.** That mode
 * calls UGameViewportClient::SetIgnoreInput(true), the viewport client belongs
 * to the ULocalPlayer, and every touch, stick, shot and loot press dies with it
 * — the exact scar Core/SarkoPlayerController.h carries from the shelter. Input
 * stays FInputModeGameOnly and Slate routes taps by hit-testing, which is why
 * the SelfHitTestInvisible rule in Construct is the whole design.
 */
class SSarkoInventoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSarkoInventoryPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASarkoCharacter>, Pawn)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Rebuilds both grids from the pawn's current view.
	 *
	 * Wholesale, and on a transfer rather than on a frame: Slate is not a tick
	 * path, and this is a few dozen widgets a handful of times per crate — the
	 * same "rebuilt on SetView" the shelter's stash list uses.
	 */
	void Refresh();

	/** All three refusal signals at once. See the .cpp for which reason lights which. */
	void PlayRefusal(int32 SlotIndex, ESarkoTakeRefusal Reason);

	/** Starts the 90 ms fade-out. The controller removes the widget when it ends. */
	void PlayExit();

	/** True once PlayExit's curve has finished, i.e. the widget can be removed. */
	bool IsExitFinished() const;

#if !UE_BUILD_SHIPPING
	/**
	 * Test-only: fires a container cell's OWN OnClicked, and only while the
	 * button is genuinely enabled — so nothing can take an item the player could
	 * not have tapped. Exists because a headless run has no fingers and pressing
	 * a Slate button for real needs hit-testing against live geometry.
	 *
	 * SButton::SimulateClick is itself `#if !UE_BUILD_SHIPPING` in the engine, so
	 * this cannot survive into a shipping build even if the guard were removed:
	 * it would fail to link.
	 */
	bool SimulateTapContainerCell(int32 SlotIndex);
#endif

private:
	TWeakObjectPtr<ASarkoCharacter> Pawn;

	/** The process-wide styles. Held as a shared ref for the panel's lifetime
	 *  because every SButton below points a raw pointer into it. */
	TSharedPtr<const FSarkoInventoryStyles> Styles;

	/** Rebuilt by Refresh; the cells hang off these. */
	TSharedPtr<SHorizontalBox> ContainerRow;
	TSharedPtr<SVerticalBox> PlayerGrid;
	TSharedPtr<STextBlock> ContainerHeader;
	TSharedPtr<STextBlock> BackpackHeader;

	/** The plate itself, which is what the entry slide moves — inside the DPI
	 *  scaler, so the 24 pt it travels is 24 points and not 24 pixels. */
	TSharedPtr<SBorder> Plate;

	/** One entry per container cell, so a headless run can press one. */
	TArray<TSharedPtr<SButton>> ContainerButtons;

	/** The bag as it was the last time Refresh ran, so the NEXT Refresh can tell
	 *  which cell received — that is what the transfer animation plays on. */
	TArray<FSarkoItemStack> PreviousBag;
	int32 ReceivingCell = INDEX_NONE;

	/** False until the first Refresh has run. The first one is the panel arriving,
	 *  not a transfer, and flashing every occupied cell on open would say "all of
	 *  this just moved" about a bag the player packed ten minutes ago. */
	bool bHasPreviousBag = false;

	/** Which container cell was refused, and why. Read by three attributes. */
	int32 RefusedSlot = INDEX_NONE;
	ESarkoTakeRefusal LastRefusal = static_cast<ESarkoTakeRefusal>(0);

	/** True while the bag is full, which is a STATE and not a flash: the header
	 *  stays amber until a cell frees up, matching the HUD's own readout. */
	bool bBagFull = false;

	/**
	 * All the motion this panel has, and all of it short.
	 *
	 * Looting does not pause the world (spec §2.4): a second of animation is a
	 * second the player is standing still in the open with a bot walking at them.
	 * Nothing here loops — FCurveSequence::Play registers an active timer, and a
	 * looping one would hold it open for as long as the panel is up, which is a
	 * frame's worth of Slate work every frame for a thing that is not moving.
	 */
	FCurveSequence EntryCurve{ 0.f, 0.140f, ECurveEaseFunction::CubicOut };
	FCurveSequence ExitCurve{ 0.f, 0.090f, ECurveEaseFunction::QuadIn };
	FCurveSequence TransferCurve{ 0.f, 0.120f, ECurveEaseFunction::CubicOut };
	FCurveSequence RefusalCurve{ 0.f, 0.240f, ECurveEaseFunction::Linear };

	bool bExiting = false;

	/** The viewport, in pixels, and the two factors derived from it. Read by the
	 *  layout attributes every pass, so a resize reflows without anything ticking. */
	FVector2D ViewportPx() const;
	float OverlayScale() const;

	/** Slot padding that pushes the panel into the bottom-right of the SAFE frame,
	 *  expressed in points because everything inside the DPI scaler is. */
	FMargin PanelPadding() const;
	FOptionalSize PanelHeight() const;

	int32 PlayerCells() const;

	/** 1 while the panel is settled, ramping on entry and falling on exit. */
	float PanelOpacity() const;
	FLinearColor PanelTint() const;
	TOptional<FSlateRenderTransform> PlateTransform() const;

	/** The amber that says "your bag is the problem", at sin(pi * lerp) — as a
	 *  choice of baked brush, because an animated tint cannot draw a
	 *  transparent-bodied rim at all. See FSarkoInventoryStyles::RefusalGlow. */
	const FSlateBrush* RefusalGlowBrush() const;
	FSlateColor BackpackHeaderColour() const;

	/** Per-cell animation, read as attributes so nothing has to tick to keep a
	 *  cell honest. Both return the identity/invisible answer for every cell that
	 *  is not the one currently animating, which is all of them almost always. */
	TOptional<FSlateRenderTransform> ContainerCellTransform(int32 SlotIndex) const;
	TOptional<FSlateRenderTransform> PlayerCellTransform(int32 SlotIndex) const;
	const FSlateBrush* TransferFlashBrush(int32 SlotIndex) const;

	TSharedRef<SWidget> BuildContainerCell(const FSarkoItemStack& Stack, int32 SlotIndex);
	TSharedRef<SWidget> BuildPlayerCell(const FSarkoItemStack& Stack, int32 SlotIndex);
	TSharedRef<SWidget> BuildCellContent(const FSarkoItemStack& Stack) const;
	TSharedRef<SWidget> BuildEmptyCell() const;

	FReply HandleTakeSlot(int32 SlotIndex);
	FReply HandleTakeAll();
};
