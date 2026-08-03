#include "UI/SarkoInventoryStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Loot/SarkoItemCatalog.h"

FLinearColor SarkoUI::CategoryColour(ESarkoItemCategory Category)
{
	// Linear, with the sRGB swatch each one is aiming at named beside it. Slate
	// colours are LINEAR and the frame is sRGB-encoded on the way out, so pasting
	// an sRGB value in here comes out roughly twice as bright as intended — the
	// shelter menu shipped a flat grey slab once for exactly that reason.
	switch (Category)
	{
	case ESarkoItemCategory::Weapon:      return FLinearColor(0.552f, 0.059f, 0.045f); // #C4453C
	case ESarkoItemCategory::Ammo:        return FLinearColor(0.694f, 0.352f, 0.027f); // #D9A02E
	case ESarkoItemCategory::Gear:        return FLinearColor(0.275f, 0.451f, 0.048f); // #8FB33E
	case ESarkoItemCategory::Med:         return FLinearColor(0.050f, 0.521f, 0.352f); // #3FBFA0
	case ESarkoItemCategory::VehiclePart: return FLinearColor(0.076f, 0.275f, 0.672f); // #4E8FD6
	case ESarkoItemCategory::Valuable:    return FLinearColor(0.397f, 0.165f, 0.687f); // #A971D8
	// Rose, at hue ~340 — the one wide gap left in the wheel, between valuable's
	// violet at 275 and weapon's red at 6. It has to be tellable from BOTH at 44
	// points across, and it is the only warm colour on the panel that is not also
	// a warning: the things you eat and drink.
	case ESarkoItemCategory::Consumable:  return FLinearColor(0.644f, 0.138f, 0.262f); // #D2688C
	case ESarkoItemCategory::Junk:
	default:                              return FLinearColor(0.195f, 0.212f, 0.238f); // #7A7F86
	}
}

FLinearColor SarkoUI::CategoryCellFill(ESarkoItemCategory Category)
{
	const FLinearColor Base = CategoryColour(Category);
	return FLinearColor(Base.R * CellFillFactor, Base.G * CellFillFactor, Base.B * CellFillFactor, 0.92f);
}

TCHAR SarkoUI::UpperChar(TCHAR Char)
{
	const uint32 Code = static_cast<uint32>(Char);

	// ASCII, for an id or a Latin name that slips into the catalog.
	if (Code - 'a' < 26u)
	{
		return static_cast<TCHAR>(Code - 32u);
	}
	// Cyrillic а..я (U+0430..U+044F) -> А..Я (U+0410..U+042F).
	if (Code - 0x0430u < 32u)
	{
		return static_cast<TCHAR>(Code - 32u);
	}
	// The four Ukrainian letters that are NOT a simple -0x20, plus the Russian ё
	// that shares the block. Written out because getting one of them wrong shows
	// up as a single wrong glyph in a 7-character label, which nobody notices.
	switch (Code)
	{
	case 0x0451: return static_cast<TCHAR>(0x0401);  // ё -> Ё
	case 0x0454: return static_cast<TCHAR>(0x0404);  // є -> Є
	case 0x0456: return static_cast<TCHAR>(0x0406);  // і -> І
	case 0x0457: return static_cast<TCHAR>(0x0407);  // ї -> Ї
	case 0x0491: return static_cast<TCHAR>(0x0490);  // ґ -> Ґ
	default:     return Char;
	}
}

FString SarkoUI::CellLabel(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return FString();
	}

	FString First;
	FString Rest;
	// The first word carries the identity in every name in the catalog:
	// "Патрони 9×18" is ammo, "Ящик з інструментами" is the box.
	if (!Name.Split(TEXT(" "), &First, &Rest))
	{
		First = Name;
	}

	for (int32 Index = 0; Index < First.Len(); ++Index)
	{
		First[Index] = UpperChar(First[Index]);
	}

	constexpr int32 MaxChars = 9;
	if (First.Len() > MaxChars)
	{
		// An ellipsis, not a hard clip: a clipped word looks like a rendering
		// fault, and a player has to be able to tell "there is more of this name"
		// from "this is the name".
		First = First.Left(MaxChars) + TEXT("…");
	}
	return First;
}

FString SarkoUI::CellLabelFor(const FSarkoItemDef* Def, FName Item)
{
	if (Def && !Def->ShortName.IsEmpty())
	{
		FString Short = Def->ShortName;
		for (int32 Index = 0; Index < Short.Len(); ++Index)
		{
			Short[Index] = UpperChar(Short[Index]);
		}
		return Short;
	}
	// No authored label: the derived cut of the display name, or of the id itself
	// when the catalog does not know the item at all. An id on a cell is ugly and
	// is meant to be — it is what items.json drifting from the backend looks like.
	return CellLabel(Def ? Def->Name : Item.ToString());
}

SarkoUI::ESarkoReloadState SarkoUI::ReloadStateFor(int32 AmmoInMagazine, int32 MagazineSize, bool bReloading,
	int32 ReserveRounds)
{
	// Reloading outranks everything, including empty: while the reload is running
	// the count is not the fact the player needs.
	if (bReloading)
	{
		return ESarkoReloadState::Reloading;
	}

	// Negative is treated as none rather than trusted. Nothing honest produces one
	// — SarkoGrid::CountItem sums positive quantities — but this decides a colour
	// on a hot path and a wrong sign here would read as "you have ammo".
	const int32 Reserve = FMath::Max(0, ReserveRounds);

	if (AmmoInMagazine <= 0)
	{
		// The split the scarcity stage made necessary. With rounds in the bag this
		// is the routine empty magazine and the pulse is the instruction. With an
		// empty bag it is DRY, which the button draws as a state rather than as an
		// invitation — there is nothing to load and no press that changes it.
		return Reserve > 0 ? ESarkoReloadState::Empty : ESarkoReloadState::Dry;
	}

	// Amber is a call to press this button. An empty bag makes the press a no-op,
	// so nagging with it would be the HUD asking for something it knows does
	// nothing — and it would drown out the one amber that still means "reload
	// now". The count on the button and the `n | 0` readout carry the bad news
	// without pretending the button is the answer to it.
	if (Reserve <= 0)
	{
		return ESarkoReloadState::Ready;
	}

	// A zero or negative magazine size is a broken config, not a divide by zero.
	// Everything is "low" then, which is the direction that nags rather than the
	// one that lies.
	if (MagazineSize <= 0)
	{
		return ESarkoReloadState::Low;
	}
	// Exactly a third is still ready — the boundary belongs to the calmer state,
	// so a 30-round magazine goes amber at 9 and not at 10.
	return (AmmoInMagazine * 3 <= MagazineSize - 1) ? ESarkoReloadState::Low : ESarkoReloadState::Ready;
}

float SarkoUI::ReloadPulseAlpha(float TimeSeconds)
{
	return 0.45f + 0.20f * FMath::Sin(TimeSeconds * 2.f * PI * 2.f);
}

FString SarkoUI::InteractLabelFor(EInteractAction Action)
{
	switch (Action)
	{
	case EInteractAction::Search:  return TEXT("ОБШУКАТИ");
	case EInteractAction::Close:   return TEXT("ЗАКРИТИ");
	case EInteractAction::Extract: return TEXT("ЕВАКУАЦІЯ");
	default:                       return FString();
	}
}

namespace
{
	/**
	 * The one place a cell's look is defined. Verified against 5.8's
	 * FSlateRoundedBoxBrush(FillColor, Radius, OutlineColor, OutlineWidth)
	 * (SlateCore/Public/Brushes/SlateRoundedBoxBrush.h).
	 *
	 * bUseBrushTransparency is set on every brush here, and that is load-bearing
	 * rather than decorative: with it false — the default — FSlateDrawElement's
	 * rounded-box path takes the outline colour VERBATIM and ignores the widget's
	 * ColorAndOpacity (DrawElementTypes.cpp), so the panel's 140 ms fade-in would
	 * fade every fill and every glyph while leaving a full set of hard rims
	 * hanging in mid-air. With it true the outline's alpha follows the tint, and
	 * the whole panel fades as one object.
	 */
	FSlateRoundedBoxBrush MakeRoundedBrush(const FLinearColor& Fill, float Radius,
		const FLinearColor& Outline, float OutlineWidth)
	{
		FSlateRoundedBoxBrush Brush(Fill, Radius, Outline, OutlineWidth);
		Brush.OutlineSettings.bUseBrushTransparency = true;
		return Brush;
	}

	/** Brighter body, same translucency. Multiplying the FLinearColor whole would
	 *  take the alpha past 1 as well, which turns a "pressed" cell into an opaque
	 *  hole in a panel whose whole point is that the world shows through it. */
	FLinearColor Brighten(const FLinearColor& Colour, float Factor)
	{
		return FLinearColor(Colour.R * Factor, Colour.G * Factor, Colour.B * Factor, Colour.A);
	}

	FButtonStyle MakeCellStyle(const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth)
	{
		FButtonStyle Style;
		Style.SetNormal(MakeRoundedBrush(Fill, SarkoUI::CellRadiusPt, Outline, OutlineWidth));
		// Pressed brightens the FILL rather than moving anything: a 44-point cell
		// under a thumb is entirely hidden, so the confirmation the player
		// actually sees is the transfer animation on the other grid. This is only
		// for the desktop pointer case, and for the instant before the finger lands.
		Style.SetHovered(MakeRoundedBrush(Brighten(Fill, 1.6f), SarkoUI::CellRadiusPt, Outline, OutlineWidth));
		Style.SetPressed(MakeRoundedBrush(Brighten(Fill, 2.2f), SarkoUI::CellRadiusPt, Outline, OutlineWidth + 0.5f));
		// A disabled cell is the empty one, and it is drawn by EmptyCell rather
		// than by a greyed-out version of this — so Disabled matches Normal and a
		// non-interactive cell does not dim into looking broken.
		Style.SetDisabled(MakeRoundedBrush(Fill, SarkoUI::CellRadiusPt, Outline, OutlineWidth));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}
}

FSarkoInventoryStyles::FSarkoInventoryStyles()
{
	// Fill × 1.0 alpha 0.92 under a full-strength rim, per category. The array is
	// sized past the enum, so a category added later without touching this file
	// still lands on a defined entry rather than reading off the end.
	static const ESarkoItemCategory Categories[] = {
		ESarkoItemCategory::Weapon, ESarkoItemCategory::Ammo, ESarkoItemCategory::Med,
		ESarkoItemCategory::Junk, ESarkoItemCategory::Valuable,
		ESarkoItemCategory::VehiclePart, ESarkoItemCategory::Gear,
		ESarkoItemCategory::Consumable,
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CellByCategory); ++Index)
	{
		CellByCategory[Index] = MakeCellStyle(SarkoUI::CategoryCellFill(ESarkoItemCategory::Junk),
			SarkoUI::CategoryColour(ESarkoItemCategory::Junk), SarkoUI::CellOutlinePt);
	}
	for (ESarkoItemCategory Category : Categories)
	{
		CellByCategory[static_cast<int32>(Category)] =
			MakeCellStyle(SarkoUI::CategoryCellFill(Category), SarkoUI::CategoryColour(Category),
				SarkoUI::CellOutlinePt);
	}

	// Visibly a slot, visibly not a thing: a thinner rim and a body barely above
	// the plate. The thinner stroke is the difference a player reads first,
	// before any colour registers.
	EmptyCell = MakeCellStyle(SarkoUI::EmptyCellFill, SarkoUI::EmptyCellOutline, SarkoUI::EmptyCellOutlinePt);

	// Hit-testing without pixels: the consumable cell it wraps is already drawn.
	// Only Pressed shows, and faintly, because the cell it covers is entirely
	// under a thumb at the moment it matters.
	InvisibleTap.SetNormal(MakeRoundedBrush(FLinearColor::Transparent, SarkoUI::CellRadiusPt,
		FLinearColor::Transparent, 0.f));
	InvisibleTap.SetHovered(MakeRoundedBrush(FLinearColor(1.f, 1.f, 1.f, 0.06f), SarkoUI::CellRadiusPt,
		FLinearColor::Transparent, 0.f));
	InvisibleTap.SetPressed(MakeRoundedBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f), SarkoUI::CellRadiusPt,
		FLinearColor::Transparent, 0.f));
	InvisibleTap.SetDisabled(MakeRoundedBrush(FLinearColor::Transparent, SarkoUI::CellRadiusPt,
		FLinearColor::Transparent, 0.f));
	InvisibleTap.SetNormalPadding(FMargin(0.f));
	InvisibleTap.SetPressedPadding(FMargin(0.f));

	// The take-all row is the panel's one loud control, so it carries the same
	// amber the HUD uses for "your bag is the problem" — at a tenth strength in
	// the body, because a solid amber bar 216 points wide would out-shout every
	// cell under it.
	TakeAllRow = MakeCellStyle(FLinearColor(0.055f, 0.030f, 0.004f, 0.92f),
		FLinearColor(0.30f, 0.165f, 0.018f, 1.f), SarkoUI::CellOutlinePt);

	PanelBrush = MakeRoundedBrush(SarkoUI::PanelPlate, SarkoUI::PanelRadiusPt,
		SarkoUI::PanelOutline, SarkoUI::PanelOutlinePt);

	// A flat 1-point rule. Radius zero on purpose: a rounded hairline reads as a
	// pill, not as a divider.
	HairlineBrush = MakeRoundedBrush(SarkoUI::Hairline, 0.f, FLinearColor::Transparent, 0.f);

	// Both animation overlays have a fully transparent body, so all that is ever
	// seen of them is the rim. Radius is one point over the cell's, so the rim
	// sits just outside the thing it is drawing attention to instead of fighting
	// with it.
	//
	// bUseBrushTransparency is deliberately left FALSE on these two and only
	// these two — see the header. With it true the outline's alpha is taken from
	// the final tint, and the final tint of a transparent-bodied brush is zero,
	// so the element is culled and the rim is never seen. False makes the
	// outline colour verbatim, which is why each rung carries its own alpha.
	for (int32 Step = 0; Step < FSarkoInventoryStyles::GlowSteps; ++Step)
	{
		const float Alpha = static_cast<float>(Step + 1) / FSarkoInventoryStyles::GlowSteps;

		FSlateRoundedBoxBrush Glow(FLinearColor::Transparent, SarkoUI::CellRadiusPt + 1.f,
			SarkoUI::AmberWarn.CopyWithNewOpacity(Alpha), 2.f);
		Glow.OutlineSettings.bUseBrushTransparency = false;
		RefusalGlow[Step] = Glow;

		FSlateRoundedBoxBrush Flash(FLinearColor::Transparent, SarkoUI::CellRadiusPt,
			FLinearColor(1.f, 1.f, 1.f, Alpha), 2.f);
		Flash.OutlineSettings.bUseBrushTransparency = false;
		TransferFlash[Step] = Flash;

		// The refused rectangle, at the size that FAILED. Same radius as a cell so
		// it reads as a cell-shaped thing, and 3 pt of stroke against the pulse's
		// 2 because it is drawn OVER occupied cells that already carry a 1.5 pt rim
		// — a ghost the same weight as the thing it overhangs reads as part of it.
		FSlateRoundedBoxBrush Ghost(FLinearColor::Transparent, SarkoUI::CellRadiusPt,
			SarkoUI::AmberWarn.CopyWithNewOpacity(Alpha), 3.f);
		Ghost.OutlineSettings.bUseBrushTransparency = false;
		RefusalGhost[Step] = Ghost;
	}
}

namespace
{
	/** Nearest rung, or null when there is not enough of it to be worth drawing.
	 *  Null and not rung zero: a permanently-drawn faintest rim around the player
	 *  grid would read as a border the panel always has. */
	const FSlateBrush* RungFor(const FSlateBrush* Ladder, int32 Steps, float Alpha)
	{
		if (Alpha <= 1.f / (Steps * 2.f))
		{
			return nullptr;
		}
		const int32 Index = FMath::Clamp(FMath::RoundToInt(Alpha * Steps) - 1, 0, Steps - 1);
		return &Ladder[Index];
	}
}

const FSlateBrush* FSarkoInventoryStyles::RefusalGlowFor(float Alpha) const
{
	return RungFor(RefusalGlow, GlowSteps, Alpha);
}

const FSlateBrush* FSarkoInventoryStyles::TransferFlashFor(float Alpha) const
{
	return RungFor(TransferFlash, GlowSteps, Alpha);
}

const FSlateBrush* FSarkoInventoryStyles::RefusalGhostFor(float Alpha) const
{
	return RungFor(RefusalGhost, GlowSteps, Alpha);
}

TSharedRef<const FSarkoInventoryStyles> FSarkoInventoryStyles::Get()
{
	// A function-local static shared ref: built once, never destroyed before
	// exit, and therefore guaranteed to outlive every SButton that points into
	// it. See the header for why that guarantee is the point.
	static const TSharedRef<const FSarkoInventoryStyles> Styles = MakeShared<FSarkoInventoryStyles>();
	return Styles;
}
