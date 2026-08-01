#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

enum class ESarkoItemCategory : uint8;

/**
 * Every colour, brush and derived string the container panel draws with.
 *
 * Separate from the panel widget because all of it is pure and must be testable
 * under -nullrhi, where no Slate application exists and a widget cannot be
 * constructed at all. The panel is where these are arranged; this is what they
 * are.
 *
 * NO BINARY ASSETS. Every brush here is an FSlateRoundedBoxBrush constructed in
 * C++ (SlateCore/Public/Brushes/SlateRoundedBoxBrush.h) and every font is
 * FCoreStyle's, which is compiled into SlateCore. That constraint is why the
 * design leans on colour, radius and outline weight instead of iconography —
 * and why the palette below is a real seven-hue wheel rather than an
 * afterthought.
 */
namespace SarkoUI
{
	/**
	 * The panel plate: near-black at 86% alpha, ~#1c1f22.
	 *
	 * Translucent and NOT a dim of the whole world. DrawOutcomeSummary dims the
	 * entire canvas at 0.55 because it is a final screen and nothing behind it
	 * can still kill you. This is the opposite case: looting does not pause the
	 * world (spec §2.4), so a bot crossing behind this panel has to stay a
	 * moving silhouette.
	 */
	const FLinearColor PanelPlate(0.012f, 0.014f, 0.018f, 0.86f);
	const FLinearColor PanelOutline(0.050f, 0.055f, 0.065f, 0.90f);
	const FLinearColor EmptyCellFill(0.020f, 0.022f, 0.026f, 0.90f);
	const FLinearColor EmptyCellOutline(0.045f, 0.050f, 0.058f, 0.90f);
	const FLinearColor Hairline(0.055f, 0.060f, 0.070f, 0.90f);

	/** ~#ffc16a. The same amber the HUD's backpack readout turns when full — the
	 *  two must agree, or "full" means one thing on the panel and another above it. */
	const FLinearColor AmberWarn(1.0f, 0.55f, 0.06f, 1.0f);

	const FLinearColor CellLabelColour(0.62f, 0.64f, 0.66f);   // ~#d0d3d5
	const FLinearColor CellCountColour(0.92f, 0.92f, 0.88f);   // ~#f7f7f3
	const FLinearColor HeaderColour(0.22f, 0.23f, 0.25f);      // ~#8b8f94

	/**
	 * How much of a category's colour goes into a cell's FILL, as opposed to its
	 * outline. 0.18, because eight adjacent 44-point cells at full saturation is
	 * a carnival and a tinted dark body under a bright rim is a rack of gear.
	 *
	 * The floor this has to clear: junk is the dimmest colour at (0.195, 0.212,
	 * 0.238), so its fill is ~0.035 linear against a plate of 0.012-0.018 — about
	 * twice the plate, which is what keeps the dullest cell reading as an object
	 * rather than a hole. Sarko.UI.CategoryColoursAreDistinctAndVisible asserts
	 * it rather than trusting this comment.
	 */
	constexpr float CellFillFactor = 0.18f;

	/**
	 * Corner radii and stroke weights, in points on the 844x390 landscape canvas
	 * — the panel lives inside an SDPIScaler, so a Slate unit there IS a point.
	 *
	 * They live beside the palette rather than beside the panel's layout numbers
	 * because they are what a cell LOOKS like, not where it is: the brush cache
	 * below is the only thing that reads them, and the brush cache is built here.
	 * A cell's rim is 1.5 pt and an empty slot's is 1 — the difference is
	 * deliberate and is half of what says "there is something in this one".
	 */
	constexpr float CellRadiusPt = 8.f;
	constexpr float CellOutlinePt = 1.5f;
	constexpr float EmptyCellOutlinePt = 1.f;
	constexpr float PanelRadiusPt = 14.f;
	constexpr float PanelOutlinePt = 1.f;

	/**
	 * A category's identity colour, used at full strength for a cell's 1.5 pt
	 * outline — the load-bearing signal, because an outline reads at 44 points
	 * where a fill does not.
	 *
	 * Seven values, six hues and one deliberate neutral: weapon 6 deg, ammo 41,
	 * gear 78, med 168, vehicle part 210, valuable 275, and junk with no hue at
	 * all. Junk being the only grey is information, not laziness. The minimum gap
	 * is 35 degrees (red to brass), which survives the small-swatch, low-
	 * luminance, glare-on-a-phone case that kills 15-degree neighbours.
	 */
	FLinearColor CategoryColour(ESarkoItemCategory Category);

	/** CategoryColour x CellFillFactor, alpha 0.92. */
	FLinearColor CategoryCellFill(ESarkoItemCategory Category);

	/**
	 * Uppercases one character, Ukrainian included.
	 *
	 * Hand-rolled and not FString::ToUpper, which is **ASCII only** — TChar::
	 * ToUpper's whole body is `Char - ((Char - 'a' < 26) << 5)` (Core/Public/Misc/
	 * Char.h), so every display name in items.json is Cyrillic and would come back
	 * unchanged. Not FText::ToUpper either: that is ICU-backed and correct, but it
	 * makes a seven-character label depend on the internationalization stack being
	 * up, and on a build with UE_ENABLE_ICU off it silently falls back to the same
	 * ASCII-only path — mixed-case labels shipped to a device while the Mac's tests
	 * stayed green. An explicit table gives the same answer everywhere.
	 *
	 * Covers the Ukrainian alphabet (including the four letters that are NOT a
	 * simple -0x20: і ї є ґ), the Russian ё that shares the block, and ASCII.
	 */
	TCHAR UpperChar(TCHAR Char);

	/**
	 * An item's display name, shortened to fit a cell: first word, uppercased,
	 * truncated to 9 characters with an ellipsis.
	 *
	 * Derived rather than authored. A 44-point cell with 4 points of padding is
	 * about seven Cyrillic glyphs wide at 8.5 pt, and items.json carries no short
	 * name; adding one would be a schema field that has to be kept in step with
	 * every name forever. This cannot drift, because there is nothing to drift
	 * from.
	 */
	FString CellLabel(const FString& Name);

	/**
	 * What the reload button is saying (spec §4.3: "the magazine count lives on
	 * it, it goes amber below a third, and it pulses when empty"). Pure, so
	 * "amber below a third" is a test rather than an eyeball — and it lives here
	 * rather than in the HUD for the same reason every colour above does: the HUD
	 * cannot be constructed under -nullrhi and this can.
	 */
	enum class ESarkoReloadState : uint8 { Ready, Low, Empty, Reloading };

	ESarkoReloadState ReloadStateFor(int32 AmmoInMagazine, int32 MagazineSize, bool bReloading);

	/** The empty button's pulse, 0.25..0.65 at 2 Hz. Bounded away from zero: a
	 *  button that vanishes on the trough reads as absent, not as urgent. */
	float ReloadPulseAlpha(float TimeSeconds);

	/**
	 * What the interact button will do. Its LABEL is contextual; its rect never
	 * is (spec §4.4: "a generic action button in a game with two actions is a
	 * guess").
	 *
	 * Extract is declared and NOT wired: an extraction is a dwell, not a press,
	 * so there is nothing to press yet — the value and its label exist as the
	 * named seam for when there is, rather than as a live button that does
	 * nothing.
	 */
	enum class EInteractAction : uint8 { None, Search, Close, Extract };

	FString InteractLabelFor(EInteractAction Action);
}

/**
 * The button styles the panel's cells are built from, constructed once and
 * owned for the process' lifetime.
 *
 * **This exists because SButton stores a raw `const FButtonStyle*`**
 * (Slate/Public/Widgets/Input/SButton.h) and points its Normal/Hovered/Pressed
 * brushes into it. A style built on the stack inside Construct is a dangling
 * pointer the moment Construct returns, and the symptom is not a crash — it is a
 * panel that draws garbage or nothing, intermittently, on a device.
 */
struct FSarkoInventoryStyles
{
	/** Process-wide, built on first use. */
	static TSharedRef<const FSarkoInventoryStyles> Get();

	/** Indexed by ESarkoItemCategory; sized past the enum so a future value cannot overrun. */
	FButtonStyle CellByCategory[8];
	FButtonStyle EmptyCell;
	FButtonStyle TakeAllRow;
	FSlateBrush PanelBrush;
	FSlateBrush HairlineBrush;

	/** How many baked opacities each fade ladder below has. Twelve reads as a
	 *  smooth ramp over 240 ms and costs twelve FSlateBrushes, once, per process. */
	static constexpr int32 GlowSteps = 12;

	/**
	 * A transparent-bodied rounded box with an amber rim, laid OVER the player
	 * grid — in twelve fixed opacities rather than one brush with an animated
	 * tint.
	 *
	 * A LADDER, and this is not a stylistic choice: it is the shape this took
	 * after a screenshot showed the pulse never drawing at all. SBorder folds the
	 * BRUSH's own tint into the colour it draws with (`BrushResource->GetTint() *
	 * InWidgetStyle * BorderBackgroundColor`, SBorder.cpp:115), so a brush with a
	 * transparent BODY has a final alpha of zero no matter what the attribute
	 * says — and FSlateDrawElement then culls the entire element, outline
	 * included (DrawElementTypes.cpp:154). Setting bUseBrushTransparency false
	 * makes the outline colour be used verbatim instead, which is the only way a
	 * transparent-bodied rim draws at all; the price is that its alpha is baked
	 * into the brush, so a fade is a choice between twelve of them.
	 */
	FSlateBrush RefusalGlow[GlowSteps];

	/** The same trick for the transfer flash: a white rim over the receiving cell. */
	FSlateBrush TransferFlash[GlowSteps];

	/**
	 * The refused rectangle's outline: amber, transparent-bodied, in the same
	 * twelve baked opacities as the two ladders above and for exactly the same
	 * reason — SBorder folds a brush's own tint into what it draws with
	 * (SBorder.cpp:115), so a transparent BODY has a final alpha of zero and
	 * FSlateDrawElement culls the whole element, outline included
	 * (DrawElementTypes.cpp:154). bUseBrushTransparency stays FALSE here, which
	 * makes the outline colour verbatim; the price is that each rung carries its
	 * own alpha.
	 *
	 * Thicker than the pulse (3 pt against 2) because it is drawn OVER occupied
	 * cells that already have a 1.5 pt rim of their own, and a ghost the same
	 * weight as the thing it is overhanging reads as part of it.
	 */
	FSlateBrush RefusalGhost[GlowSteps];

	/** The rung nearest Alpha, or null below the first — a null BorderImage draws
	 *  nothing, which is exactly the resting state all three of these want. */
	const FSlateBrush* RefusalGlowFor(float Alpha) const;
	const FSlateBrush* TransferFlashFor(float Alpha) const;
	const FSlateBrush* RefusalGhostFor(float Alpha) const;

	FSarkoInventoryStyles();
};
