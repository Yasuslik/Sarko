#pragma once

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "Types/SlateStructs.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

// By value in PreviousBag and by const-ref in BuildCell, so the complete
// USTRUCT has to be visible here rather than forward-declared.
#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoItemGrid.h"

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
	 * Top and bottom. Twelve and not fourteen, because the vertical stack adds up
	 * to the 244 pt the panel is specified at only with twelve
	 * (12 + 44 + 6 + 44 + 12 + 16 + 6 + 92 + 12), and the height is the axis that
	 * decides whether the panel clears the HUD's health bar. The horizontal
	 * padding stays 14, which is what makes the width exactly 318.
	 */
	constexpr float PanelPadYPt = 12.f;

	/** Between the pockets page and the backpack page. */
	constexpr float PagesGapPt = 10.f;

	/**
	 * 14 + (92 pockets + 10 gap + 188 backpack) + 14.
	 *
	 * A CONSTANT since 2026-08-05: the panel no longer grows with capacity, so
	 * finding a bag mid-raid cannot reflow the thing the player is reading. The
	 * 4x2 backpack page is drawn whether or not one is worn — dimmed and labelled
	 * НЕМАЄ РЮКЗАКА when it is not — because that is the space a bag would give
	 * you, shown beside 2-wide pockets a 3-wide rifle cannot enter.
	 */
	constexpr float PanelWidthPt = 318.f;

	/** 12 + 44 take-all + 6 + 44 container + 12 divider + 16 header + 6 + 92 cells + 12. */
	constexpr float PanelHeightPt = 244.f;

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
	 * above it, stacked rather than run together on one line: at 190 pt of inner
	 * width,
	 * "ОБШУК · MILITARY" and "ЗАБРАТИ ВСЕ" on one row overlapped each other, and
	 * a header printed through a button label is the first thing that makes a
	 * screen look unfinished.
	 */
	constexpr float SectionLabelPt = 9.f;
	constexpr float TierPt = 13.f;

	/** How far the panel slides on entry. */
	constexpr float EntrySlidePt = 24.f;

	/** In from the safe frame's LEFT edge, and up from its bottom. The bottom
	 *  number keeps the panel clear of the home indicator; the left number is the
	 *  half of spec §4.5 that moves it off the aim thumb. */
	constexpr float PanelLeftInsetPt = 16.f;
	constexpr float PanelBottomInsetPt = 20.f;

	/**
	 * Where the panel goes, in whatever unit SafeFrame is in (pixels for the HUD,
	 * points for the widget — PointScale is the bridge). Pure, so the one property
	 * that decides whether a player can see the bot walking at them is unit tested
	 * without a viewport, a widget or a Slate application.
	 *
	 * Bottom-LEFT since 2026-08-05 (spec §4.5): it used to sit over the aim stick,
	 * where a thumb reaching to shoot could land on a cell instead. Moving it is
	 * only half the fix — the other half is that the move stick sleeps while it is
	 * open (SarkoInput::IsMoveStickSuppressed).
	 *
	 * It takes no cell count, and that is what makes "one size, whatever the pawn
	 * is carrying" structural rather than a promise.
	 */
	FBox2D InventoryPanelRect(FBox2D SafeFrame, float PointScale);

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
 * Bottom-LEFT of the safe frame and translucent, because looting does not pause
 * the world (spec §2.4/§5): the pawn at screen centre and the whole right half
 * of the screen are never covered, and a bot crossing behind the plate stays a
 * moving silhouette rather than disappearing behind a dim.
 *
 * It sat bottom-RIGHT until 2026-08-05, on top of the aim stick, passing touches
 * through everywhere except its cells — so a thumb reaching to shoot could land
 * on a cell instead. Spec §4.5 moved it here and put the move stick to sleep
 * while it is open: looting already requires standing still, so movement is the
 * input you can afford to lose, and shooting is not.
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
	TSharedPtr<STextBlock> ContainerHeader;

	/** One box per carry page, refilled by Refresh. Pockets is always page 0, and
	 *  the backpack page is drawn even when no bag is worn — dimmed, so the shape
	 *  of what a bag would give you is visible beside the pockets it dwarfs. */
	TSharedPtr<class SBox> PocketPage;
	TSharedPtr<class SBox> BackpackPage;
	TSharedPtr<STextBlock> PocketHeader;
	TSharedPtr<STextBlock> BackpackHeader;

	/** "НЕ ВЛІЗЕ 2×1" for the 240 ms of a NoSpace refusal, then empty. */
	TSharedPtr<STextBlock> RefusalNote;

	/** The refused rectangle and where to draw it, valid only while RefusalCurve
	 *  is playing and LastRefusal is NoSpace. */
	FSarkoGridSlot RefusedGhost;

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

	/** 1 while the panel is settled, ramping on entry and falling on exit. */
	float PanelOpacity() const;
	FLinearColor PanelTint() const;
	TOptional<FSlateRenderTransform> PlateTransform() const;

	/** The amber that says "your bag is the problem", at sin(pi * lerp) — as a
	 *  choice of baked brush, because an animated tint cannot draw a
	 *  transparent-bodied rim at all. See FSarkoInventoryStyles::RefusalGlow. */
	const FSlateBrush* RefusalGlowBrush() const;
	FSlateColor BackpackHeaderColour() const;

	/**
	 * The ghost: the exact w x h that would not fit, drawn at RefusalAnchor's gap.
	 *
	 * Positioned by padding an outer box rather than by a canvas offset, which is
	 * the pattern PanelPadding already uses: the outer box's desired size is the
	 * offset plus the inner box's, so a top-left-aligned overlay slot puts the
	 * inner box exactly where the layout says without any clipping.
	 */
	FMargin GhostPadding() const;
	FOptionalSize GhostWidth() const;
	FOptionalSize GhostHeight() const;
	const FSlateBrush* RefusalGhostBrush() const;

	/** Per-cell animation, read as attributes so nothing has to tick to keep a
	 *  cell honest. Both return the identity/invisible answer for every cell that
	 *  is not the one currently animating, which is all of them almost always. */
	TOptional<FSlateRenderTransform> ContainerCellTransform(int32 SlotIndex) const;
	TOptional<FSlateRenderTransform> PlayerCellTransform(int32 SlotIndex) const;
	const FSlateBrush* TransferFlashBrush(int32 SlotIndex) const;

	// What a cell IS lives in UI/SarkoCellWidgets.h, shared with the shelter's
	// stash (spec §2: one visual language for "things you own"). What is left here
	// is what only this panel does: a tappable container cell, and a carry cell
	// carrying this panel's transfer animation.
	TSharedRef<SWidget> BuildContainerCell(const FSarkoItemStack& Stack, int32 SlotIndex);

	/** The hook SarkoUI::BuildGridPage calls for each placed carry stack: wraps
	 *  the shared cell in this panel's transfer flash and receive-scale. */
	TSharedRef<SWidget> DecorateCarryCell(int32 SlotIndex, TSharedRef<SWidget> Cell);

	FReply HandleTakeSlot(int32 SlotIndex);
	FReply HandleTakeAll();
};
