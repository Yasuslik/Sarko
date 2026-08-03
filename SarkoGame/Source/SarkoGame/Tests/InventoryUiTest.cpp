#include "Misc/AutomationTest.h"

#include "Core/SarkoPlayerController.h"
#include "Loot/SarkoItemCatalog.h"
#include "Loot/SarkoItemGrid.h"
#include "Loot/SarkoLootTable.h"
#include "UI/SarkoCellWidgets.h"
#include "UI/SarkoInventoryPanel.h"
#include "UI/SarkoInventoryStyle.h"
#include "UI/SarkoUiScale.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCategoryColoursAreDistinctAndVisible,
	"Sarko.UI.CategoryColoursAreDistinctAndVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCategoryColoursAreDistinctAndVisible::RunTest(const FString& Parameters)
{
	// Colour is the substitute for icons: this project ships no binary assets, so
	// a cell's category has to be legible from its hue at 44 points across. Two
	// properties make that work, and neither is safe to eyeball.
	const TArray<ESarkoItemCategory> All = {
		ESarkoItemCategory::Weapon, ESarkoItemCategory::Ammo, ESarkoItemCategory::Med,
		ESarkoItemCategory::Junk, ESarkoItemCategory::Valuable,
		ESarkoItemCategory::VehiclePart, ESarkoItemCategory::Gear,
		ESarkoItemCategory::Consumable,
	};

	// 1. Every category is a different colour. A duplicate would silently merge
	//    two kinds of loot into one visual signal.
	for (int32 A = 0; A < All.Num(); ++A)
	{
		for (int32 B = A + 1; B < All.Num(); ++B)
		{
			const FLinearColor CA = SarkoUI::CategoryColour(All[A]);
			const FLinearColor CB = SarkoUI::CategoryColour(All[B]);
			const float Distance = FMath::Abs(CA.R - CB.R) + FMath::Abs(CA.G - CB.G) + FMath::Abs(CA.B - CB.B);
			TestTrue(*FString::Printf(TEXT("categories %d and %d are visibly different (L1 %.3f)"),
					static_cast<int32>(All[A]), static_cast<int32>(All[B]), Distance),
				Distance > 0.15f);
		}
	}

	// 2. Even the dullest cell fill sits clearly above the panel plate, or a
	//    junk cell reads as a hole in the panel rather than as a slot with
	//    something in it. Junk is the floor by construction — it is the one
	//    deliberately hueless colour.
	const float PlateLuma = SarkoUI::PanelPlate.R + SarkoUI::PanelPlate.G + SarkoUI::PanelPlate.B;
	for (ESarkoItemCategory Category : All)
	{
		const FLinearColor Fill = SarkoUI::CategoryCellFill(Category);
		const float FillLuma = Fill.R + Fill.G + Fill.B;
		TestTrue(*FString::Printf(TEXT("category %d's fill (%.3f) is at least twice the plate (%.3f)"),
				static_cast<int32>(Category), FillLuma, PlateLuma),
			FillLuma >= PlateLuma * 2.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoCellLabelFitsACell,
	"Sarko.UI.CellLabelFitsACell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoCellLabelFitsACell::RunTest(const FString& Parameters)
{
	// The FALLBACK derivation, which is all this is since items.json started
	// authoring `short` (see Sarko.UI.ShortNamesAreAuthoredAndDistinct). It stays
	// tested because it is still what an id the catalog does not know falls back
	// to, and that path is the visible symptom of drift with the backend.
	TestEqual(TEXT("a multi-word name keeps its first word"),
		SarkoUI::CellLabel(TEXT("Ящик з інструментами")), FString(TEXT("ЯЩИК")));
	TestEqual(TEXT("a name with a number keeps the word, not the number"),
		SarkoUI::CellLabel(TEXT("Патрони 9×18")), FString(TEXT("ПАТРОНИ")));
	TestEqual(TEXT("a short name survives whole"),
		SarkoUI::CellLabel(TEXT("Бинт")), FString(TEXT("БИНТ")));
	TestEqual(TEXT("a long single word is truncated with an ellipsis, never clipped mid-cell"),
		SarkoUI::CellLabel(TEXT("Обезболювальне")), FString(TEXT("ОБЕЗБОЛЮВ…")));
	TestEqual(TEXT("an empty name does not crash and draws nothing"),
		SarkoUI::CellLabel(FString()), FString());

	// The four Ukrainian letters that are not a simple -0x20, in one word that
	// contains all of them. FString::ToUpper is ASCII-only (TChar::ToUpper), so
	// without SarkoUI::UpperChar every one of these labels would come back in the
	// mixed case items.json wrote it in — and the Mac's tests would not have said
	// so, because they would have been comparing against the same wrong answer.
	TestEqual(TEXT("і, ї, є and ґ uppercase to І, Ї, Є and Ґ"),
		SarkoUI::CellLabel(TEXT("іїєґ")), FString(TEXT("ІЇЄҐ")));

	// Every display name the shipped catalog can hand this must survive it: no
	// empty label (a cell with nothing written on it is a cell you cannot
	// identify) and nothing longer than the ten characters a cell can hold.
	FSarkoItemCatalog Catalog;
	FString Error;
	if (TestTrue(TEXT("the shipped catalog loads"), SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error)))
	{
		for (const FSarkoItemDef& Def : Catalog.Items)
		{
			const FString Label = SarkoUI::CellLabel(Def.Name);
			TestTrue(*FString::Printf(TEXT("'%s' produces a label"), *Def.Name), !Label.IsEmpty());
			TestTrue(*FString::Printf(TEXT("'%s' -> '%s' fits a cell"), *Def.Name, *Label), Label.Len() <= 10);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoShortNamesAreAuthoredAndDistinct,
	"Sarko.UI.ShortNamesAreAuthoredAndDistinct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoShortNamesAreAuthoredAndDistinct::RunTest(const FString& Parameters)
{
	// The flaw this replaces, from a frame of the shelter: ПАТР…, АПТЕ…, ОБЕЗ…,
	// АРМО…, МЕТА…, МІДН…, ЦИГА…, ЛАНЦ…, ГОРІЛ…, КОНС… — ten of sixteen cells
	// ellipsed. Six Cyrillic glyphs is not enough for Ukrainian names, and the
	// failure that matters is not the ellipsis: it is two junk greys that both read
	// ЛОМ…, because telling two items of the SAME hue apart is the only job the
	// label has that the category colour does not already do.
	//
	// So the label is AUTHORED per item, and this is what makes that mandatory for
	// the file that ships. The parser deliberately treats `short` as optional (a
	// missing one must not make the whole catalog unloadable and leave the game
	// with no loot at all), which means the shipped file is exactly what needs a
	// guard.
	FSarkoItemCatalog Catalog;
	FString Error;
	if (!TestTrue(TEXT("the shipped catalog loads"), SarkoLoot::LoadItemCatalogFromDisk(Catalog, Error)))
	{
		AddError(FString::Printf(TEXT("%s — nothing below can be checked"), *Error));
		return false;
	}

	TMap<FString, FName> Seen;
	for (const FSarkoItemDef& Def : Catalog.Items)
	{
		// 1. Present. An item without one silently falls back to the derivation
		//    that produced the ellipses in the first place.
		if (!TestTrue(*FString::Printf(TEXT("'%s' (%s) has an authored short name"),
				*Def.Id.ToString(), *Def.Name), !Def.ShortName.IsEmpty()))
		{
			continue;
		}

		// 2. Two to six characters. Six is what a 2x2 or wider cell holds; two is
		//    the floor because ПМ is the pistol's actual name and padding it would
		//    invent a letter. A 1x1 cell is stricter — see 4.
		TestTrue(*FString::Printf(TEXT("'%s' is 2..6 characters (%d)"), *Def.ShortName, Def.ShortName.Len()),
			Def.ShortName.Len() >= 2 && Def.ShortName.Len() <= 6);

		// 3. One word. A cell draws a single line and a space in it is either a
		//    wrap or an ellipsis, both of which waste the room the field bought.
		TestFalse(*FString::Printf(TEXT("'%s' is one word"), *Def.ShortName),
			Def.ShortName.Contains(TEXT(" ")));

		// 4. Five for a single-cell item. A 44 pt cell with 4 pt of padding holds
		//    about five Cyrillic capitals at 7.5 pt — measured off the frame, where
		//    "ОБЕЗ…" was four plus an ellipsis in that space. A six-character label
		//    on a 1x1 would be ellipsed again, which is the bug.
		if (Def.Width <= 1 && Def.Height <= 1)
		{
			TestTrue(*FString::Printf(TEXT("'%s' is a 1x1 item, so its label is at most 5 (%d)"),
					*Def.Id.ToString(), Def.ShortName.Len()),
				Def.ShortName.Len() <= 5);
		}

		// 5. Unique. THE point of the field: two cells reading the same word is the
		//    failure the ellipses were causing, and authoring them by hand is
		//    exactly how it comes back.
		const FString Label = SarkoUI::CellLabelFor(&Def, Def.Id);
		if (const FName* Clash = Seen.Find(Label))
		{
			AddError(FString::Printf(TEXT("'%s' and '%s' both draw '%s' — they are indistinguishable in the grid"),
				*Clash->ToString(), *Def.Id.ToString(), *Label));
		}
		Seen.Add(Label, Def.Id);

		// 6. What a cell actually draws is the authored label, uppercased, with
		//    nothing cut off it.
		TestEqual(*FString::Printf(TEXT("'%s' draws its authored label whole"), *Def.Id.ToString()),
			Label, Def.ShortName);
		TestFalse(*FString::Printf(TEXT("'%s' is not ellipsed"), *Label), Label.Contains(TEXT("…")));
	}

	// And the two paths with no authored label. An unknown id must still print
	// SOMETHING — an id on a cell is what drift with the backend looks like, and
	// hiding it would hide an item the player owns.
	TestEqual(TEXT("an item with no short name falls back to the derived cut"),
		SarkoUI::CellLabelFor(nullptr, FName(TEXT("mystery_thing"))), FString(TEXT("MYSTERY_T…")));
	FSarkoItemDef NoShort;
	NoShort.Name = TEXT("Обезболювальне");
	TestEqual(TEXT("so does a definition that has a name but no short name"),
		SarkoUI::CellLabelFor(&NoShort, FName(TEXT("x"))), FString(TEXT("ОБЕЗБОЛЮВ…")));
	FSarkoItemDef LowerCase;
	LowerCase.ShortName = TEXT("ланц");
	TestEqual(TEXT("and a label typed in the wrong case is uppercased, not shipped as typed"),
		SarkoUI::CellLabelFor(&LowerCase, FName(TEXT("x"))), FString(TEXT("ЛАНЦ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMultiCellLabelScalesWithItsRectangle,
	"Sarko.UI.MultiCellLabelScalesWithItsRectangle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMultiCellLabelScalesWithItsRectangle::RunTest(const FString& Parameters)
{
	// A 3x2 frame and a 2x2 wheel drew like a 1x1: a 7.5 pt label in the top-left
	// corner and a 10 pt number in the far bottom-right of a 140x92 pt rectangle,
	// 150 pt apart. That reads as an unfilled panel with a stray tag on it rather
	// than as an object, and it is the second thing a frame of this screen showed.
	const FIntPoint One(1, 1);

	// The anchor: a single cell is EXACTLY what it was, because that size was
	// judged on a frame and works. A fix for truncation that shrinks or grows the
	// case that already reads correctly is not a fix.
	TestEqual(TEXT("a 1x1 label is exactly CellLabelPt"),
		SarkoUI::CellLabelPtFor(One), SarkoUI::CellLabelPt);
	TestEqual(TEXT("a 1x1 count is exactly CellCountPt"),
		SarkoUI::CellCountPtFor(One), SarkoUI::CellCountPt);
	TestFalse(TEXT("and a 1x1 is not laid out as a plate"), SarkoUI::IsMultiCell(One));
	TestTrue(TEXT("while everything larger is"),
		SarkoUI::IsMultiCell(FIntPoint(2, 1)) && SarkoUI::IsMultiCell(FIntPoint(1, 2)) &&
		SarkoUI::IsMultiCell(FIntPoint(2, 2)) && SarkoUI::IsMultiCell(FIntPoint(3, 2)));

	// Bigger rectangle, bigger type — never smaller, and never past the cap.
	const TArray<FIntPoint> Sizes = { { 1, 1 }, { 2, 1 }, { 2, 2 }, { 3, 2 }, { 4, 2 } };
	float Previous = 0.f;
	for (const FIntPoint Size : Sizes)
	{
		const float Pt = SarkoUI::CellLabelPtFor(Size);
		TestTrue(*FString::Printf(TEXT("%dx%d's label (%.2f) is never below a 1x1's"), Size.X, Size.Y, Pt),
			Pt >= SarkoUI::CellLabelPt - KINDA_SMALL_NUMBER);
		TestTrue(*FString::Printf(TEXT("%dx%d's label (%.2f) is bounded"), Size.X, Size.Y, Pt),
			Pt <= SarkoUI::CellLabelMaxPt + KINDA_SMALL_NUMBER);
		TestTrue(*FString::Printf(TEXT("%dx%d's label does not shrink as the cell grows"), Size.X, Size.Y),
			Pt >= Previous - KINDA_SMALL_NUMBER);
		Previous = Pt;

		// And the label STACKED OVER the count fits the interior height, padding
		// clear on both sides. 1.25 lines per point of type is a generous estimate
		// of Roboto's line height (~1.17), so this is the pessimistic reading: the
		// plate layout must not be able to put type on a cell's own rim.
		const FVector2D Extent = SarkoUI::CellExtentPt(Size);
		const float Stacked = 1.25f * (SarkoUI::CellLabelPtFor(Size) + SarkoUI::CellCountPtFor(Size));
		TestTrue(*FString::Printf(TEXT("%dx%d's label over its count (%.1f pt) fits %.0f pt of interior"),
				Size.X, Size.Y, Stacked, Extent.Y - 2.0 * SarkoUI::CellPadPt),
			Stacked < Extent.Y - 2.f * SarkoUI::CellPadPt);
	}

	// The two footprints the flaw was reported on actually grow, and to the cap —
	// a rule that is monotonic but never moves would pass everything above.
	TestTrue(TEXT("a 2x2 wheel's label is visibly larger than a cell's"),
		SarkoUI::CellLabelPtFor(FIntPoint(2, 2)) > SarkoUI::CellLabelPt * 1.5f);
	TestEqual(TEXT("and a 3x2 frame's is at the cap"),
		SarkoUI::CellLabelPtFor(FIntPoint(3, 2)), SarkoUI::CellLabelMaxPt);

	// The count keeps the 4:3 relationship it has on a 1x1 at every footprint, so
	// the number stays the thing the eye stops on.
	for (const FIntPoint Size : Sizes)
	{
		TestTrue(*FString::Printf(TEXT("%dx%d's count is the larger of the two"), Size.X, Size.Y),
			SarkoUI::CellCountPtFor(Size) > SarkoUI::CellLabelPtFor(Size));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOverlayScaleDividesOutTheLayerManager,
	"Sarko.UI.OverlayScaleDividesOutTheLayerManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoOverlayScaleDividesOutTheLayerManager::RunTest(const FString& Parameters)
{
	// SGameLayerManager wraps the viewport overlay in its OWN SDPIScaler
	// (SGameLayerManager.cpp:113, fed by UUserInterfaceSettings::
	// GetDPIScaleBasedOnSize). A widget that also scales itself compounds with
	// it — on a 2556x1179 phone the engine curve gives 1.09, so the panel would
	// render 9% larger than the points it claims and would not line up with the
	// HUD it is drawn over. The HUD does not go through that path at all: its
	// canvas has a DPI scale of exactly 1.
	const FVector2D Phone(2556.f, 1179.f);
	const float Raw = SarkoUI::PointScaleForViewport(Phone);
	const float Overlay = SarkoUI::OverlayPointScale(Phone);

	TestTrue(TEXT("the overlay scale is positive"), Overlay > 0.f);
	TestTrue(TEXT("and no larger than the raw point scale — it only ever divides out"),
		Overlay <= Raw + KINDA_SMALL_NUMBER);

	// The product is the thing that must equal the raw scale: whatever the layer
	// manager multiplies by, this divides by, so the two cancel and a point is a
	// point. Asserted against the engine's own function rather than a copied
	// constant, so a project that changes UIScaleCurve does not silently break.
	const float LayerScale = SarkoUI::GameLayerDpiScale(Phone);
	TestTrue(TEXT("overlay x layer == raw, so the compounding cancels"),
		FMath::IsNearlyEqual(Overlay * LayerScale, Raw, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPanelSitsInTheLeftHalfClearOfTheAimThumb,
	"Sarko.UI.PanelSitsInTheLeftHalfClearOfTheAimThumb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPanelSitsInTheLeftHalfClearOfTheAimThumb::RunTest(const FString& Parameters)
{
	// Spec §4.5: the panel used to sit bottom-RIGHT, on top of the aim stick,
	// passing touches through everywhere except its cells — so a thumb reaching
	// to shoot could land on a cell instead. That is a genuine hazard and this is
	// the assert that stops it coming back.
	const FVector2D Viewport(2556.f, 1179.f);
	const FBox2D Safe = SarkoInput::SafeFrame(Viewport);
	const float Scale = SarkoUI::PointScaleForViewport(Viewport);
	const FBox2D Panel = SarkoUI::InventoryPanelRect(Safe, Scale);

	TestTrue(TEXT("the panel starts at the safe frame's left edge, not its right"),
		Panel.Min.X < Safe.GetCenter().X);
	TestTrue(TEXT("and ends before the midline, so the whole right half is free"),
		Panel.Max.X <= Viewport.X * 0.5f);
	TestTrue(TEXT("it is bottom-anchored, clear of the home indicator"),
		Panel.Max.Y < Safe.Max.Y && Panel.Max.Y > Safe.GetCenter().Y);

	// And it does not eat the pawn, who is at the centre of the screen.
	TestTrue(TEXT("the pawn at screen centre is not under the panel"),
		Panel.Max.X < Viewport.X * 0.5f);

	// The HUD's health bar occupies y 16..27 pt at the top right. Overlapping it
	// would hide the one readout that says you are dying while you stand still.
	TestTrue(TEXT("it clears the health bar's row"), Panel.Min.Y > 40.f * Scale);

	// One size, whatever the pawn is carrying: the rect no longer takes a cell
	// count at all, which is what makes that guarantee structural.
	const FBox2D Again = SarkoUI::InventoryPanelRect(Safe, Scale);
	TestEqual(TEXT("the rect is a pure function of the frame"), Panel.Min.X, Again.Min.X);
	TestEqual(TEXT("the rect is a pure function of the frame"), Panel.Min.Y, Again.Min.Y);

	// And the thumb column is nowhere near it, at any of three sizes.
	for (const FVector2D Size : { FVector2D(2556.f, 1179.f), FVector2D(1560.f, 720.f), FVector2D(1280.f, 720.f) })
	{
		const FBox2D Frame = SarkoInput::SafeFrame(Size);
		const float PointScale = SarkoUI::PointScaleForViewport(Size);
		const FBox2D Plate = SarkoUI::InventoryPanelRect(Frame, PointScale);
		TestFalse(*FString::Printf(TEXT("at %.0fx%.0f the panel does not touch the interact button"), Size.X, Size.Y),
			Plate.Intersect(SarkoInput::InteractButtonRect(Frame, PointScale)));
		TestFalse(*FString::Printf(TEXT("at %.0fx%.0f it does not touch the reload button either"), Size.X, Size.Y),
			Plate.Intersect(SarkoInput::ReloadButtonRect(Frame, PointScale)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoPanelGeometryIsFixed,
	"Sarko.UI.PanelGeometryIsFixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoPanelGeometryIsFixed::RunTest(const FString& Parameters)
{
	// The panel is one size, always. It used to grow with capacity, which meant
	// finding a backpack mid-raid reflowed the thing the player was reading. Now
	// the backpack page is drawn whether or not one is worn — dimmed when it is
	// not, which is the space a bag would give you, next to the 2-wide pockets a
	// 3-wide rifle cannot enter.
	TestEqual(TEXT("14 + (92 + 10 + 188) + 14"), SarkoUI::PanelWidthPt, 318.f);
	TestEqual(TEXT("12 + 44 + 6 + 44 + 12 + 16 + 6 + 92 + 12"), SarkoUI::PanelHeightPt, 244.f);

	// The pages, in points, from the same identity the cells use.
	TestEqual(TEXT("the pocket page is 92 square"), SarkoUI::CellExtentPt(FIntPoint(2, 2)).X, 92.0);
	TestEqual(TEXT("the backpack page is 188 x 92"), SarkoUI::CellExtentPt(FIntPoint(4, 2)).X, 188.0);
	TestEqual(TEXT("the container row is 188 wide, so it fits inside the carry band"),
		SarkoUI::CellExtentPt(FIntPoint(SarkoLoot::ContainerCells, 1)).X, 188.0);

	// The width the two pages and the padding actually add up to. Written as the
	// sum rather than as 318 a second time, so an edit to a page or a gap fails
	// here instead of drawing a page off the edge of the plate.
	TestEqual(TEXT("the pages and the padding fill the panel exactly"),
		SarkoUI::CellExtentPt(FIntPoint(2, 2)).X + SarkoUI::PagesGapPt
			+ SarkoUI::CellExtentPt(FIntPoint(4, 2)).X + SarkoUI::PanelPadPt * 2.0,
		static_cast<double>(SarkoUI::PanelWidthPt));

	// The 44 pt tap-target rule applies to the ONE thing in this panel that is
	// tappable. The carry cells are SelfHitTestInvisible by design — the thumb
	// aims through them — so they carry no minimum.
	TestTrue(TEXT("a container cell clears the tap-target minimum"),
		SarkoUI::CellSizePt >= 44.f);
	TestTrue(TEXT("so does the take-all row"), SarkoUI::TakeAllRowPt >= 44.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRefusalAnchorOverhangsWhatBlockedIt,
	"Sarko.UI.RefusalAnchorOverhangsWhatBlockedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRefusalAnchorOverhangsWhatBlockedIt::RunTest(const FString& Parameters)
{
	// Spec §5: "The refusal must say why — no space of that shape — and the panel
	// should show the shape that failed." A ghost dropped at (0,0) would sit on a
	// full row and say nothing. It has to land on the gap the player is looking
	// at, so that it visibly runs OUT of that gap into the cell in the way.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const TArray<FSarkoGridPage> Pages = SarkoGrid::CarryPages(true, FIntPoint(2, 2), FIntPoint(4, 2));

	// Backpack row 0 fills first, then row 1 up to (1,1); the two cells left are
	// (2,1) and (3,1) — adjacent, so a 2x1 WOULD fit and must not be refused.
	TArray<FSarkoItemStack> Bag = {
		FSarkoItemStack{ TEXT("bandage"), 1 },        // pockets (0,0)
		FSarkoItemStack{ TEXT("medkit"), 1 },         // pockets (1,0)
		FSarkoItemStack{ TEXT("painkillers"), 1 },    // pockets (0,1)
		FSarkoItemStack{ TEXT("chain"), 1 },          // pockets (1,1) — pockets full
		FSarkoItemStack{ TEXT("scrap_metal"), 1 },    // backpack (0,0)
		FSarkoItemStack{ TEXT("copper_wire"), 1 },    // backpack (1,0)
		FSarkoItemStack{ TEXT("duct_tape"), 1 },      // backpack (2,0)
		FSarkoItemStack{ TEXT("canned_food"), 1 },    // backpack (3,0) — row 0 full
		FSarkoItemStack{ TEXT("vodka"), 1 },          // backpack (0,1)
		FSarkoItemStack{ TEXT("cigarettes"), 1 },     // backpack (1,1)
	};
	const TArray<FSarkoGridSlot> Placed = SarkoGrid::Place(Bag, Catalog, Pages);
	for (const FSarkoGridSlot& Slot : Placed)
	{
		TestTrue(TEXT("the setup itself fits"), Slot.IsPlaced());
	}

	TArray<FSarkoItemStack> Copy = Bag;
	TestEqual(TEXT("a 2x1 still fits the adjacent pair"),
		SarkoGrid::AddToGrid(Copy, Catalog, Pages, TEXT("toolbox"), 1), 0);

	// Now fill (2,1) so the only free cell is (3,1): a lone gap, and a 2x1 cannot
	// use it. THIS is the case the ghost exists for.
	Bag.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 1 });      // backpack (2,1)
	const TArray<FSarkoGridSlot> Nearly = SarkoGrid::Place(Bag, Catalog, Pages);

	const FSarkoGridSlot Ghost = SarkoGrid::RefusalAnchor(Nearly, Pages, FIntPoint(2, 1));
	TestTrue(TEXT("the ghost is anchored somewhere"), Ghost.IsPlaced());
	TestEqual(TEXT("on the page that has the gap"), Ghost.Page, 1);
	TestEqual(TEXT("drawn at the size that FAILED, not at the size that fits"), Ghost.W, 2);
	TestEqual(TEXT("drawn at the size that FAILED, not at the size that fits"), Ghost.H, 1);

	// The gap is at (3,1) and the ghost is 2 wide, so it is pulled back to x 2 —
	// on the page rather than half off the plate, where a missing edge would read
	// as a clipping fault. That is the frame this whole signal exists for: half of
	// the ghost is on the gap the player is looking at and half is on the cell
	// that is actually in the way.
	TestEqual(TEXT("pulled back so the whole rectangle stays on the page"), Ghost.X, 2);
	TestEqual(TEXT("in the row the gap is in"), Ghost.Y, 1);
	TestTrue(TEXT("the gap is still under the ghost"),
		Ghost.X <= 3 && 3 < Ghost.X + Ghost.W);
	TestTrue(TEXT("and so is the occupied cell that blocked it"),
		Ghost.X <= 2 && 2 < Ghost.X + Ghost.W);
	TestTrue(TEXT("the ghost stays inside the page it is drawn on"),
		Ghost.X + Ghost.W <= Pages[1].Columns);

	// A completely full grid still has to anchor somewhere, or there is no ghost
	// and the loudest refusal is the quietest one.
	TArray<FSarkoItemStack> Full;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		SarkoGrid::AddToGrid(Full, Catalog, Pages, TEXT("chain"), 1);
	}
	const FSarkoGridSlot NoRoom =
		SarkoGrid::RefusalAnchor(SarkoGrid::Place(Full, Catalog, Pages), Pages, FIntPoint(2, 1));
	TestTrue(TEXT("a full grid still anchors the ghost, at the origin"), NoRoom.IsPlaced());
	TestEqual(TEXT("at the origin"), NoRoom.X, 0);
	TestEqual(TEXT("at the origin"), NoRoom.Y, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoMoveStickIsSuppressedOnlyWhileAPanelIsOpen,
	"Sarko.Input.MoveStickIsSuppressedOnlyWhileAPanelIsOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoMoveStickIsSuppressedOnlyWhileAPanelIsOpen::RunTest(const FString& Parameters)
{
	// ONE function, so spec §5's fallback — "if it reads as a bug in play, shrink
	// the panel rather than restore movement under it" — is a one-line change
	// here and nowhere else. If this ever grows a second condition, it grows it
	// in this function.
	TestTrue(TEXT("a panel is open, so the left thumb does nothing"),
		SarkoInput::IsMoveStickSuppressed(true));
	TestFalse(TEXT("no panel, so movement is movement"),
		SarkoInput::IsMoveStickSuppressed(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRefusalShakeStartsAndEndsAtRest,
	"Sarko.UI.RefusalShakeStartsAndEndsAtRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRefusalShakeStartsAndEndsAtRest::RunTest(const FString& Parameters)
{
	// A shake that does not return to zero leaves the cell permanently offset by
	// a few points — which nobody notices as an animation bug and everybody
	// notices as a grid that is subtly crooked.
	TestTrue(TEXT("starts at rest"), FMath::IsNearlyZero(SarkoUI::RefusalShakeOffsetPt(0.f), 0.001f));
	TestTrue(TEXT("ends at rest"), FMath::IsNearlyZero(SarkoUI::RefusalShakeOffsetPt(1.f), 0.001f));
	// Two full cycles, so it reads as "no" rather than as a glitch.
	TestTrue(TEXT("swings both ways"),
		SarkoUI::RefusalShakeOffsetPt(0.125f) > 3.f && SarkoUI::RefusalShakeOffsetPt(0.375f) < -3.f);
	TestTrue(TEXT("never exceeds the amplitude, so it cannot leave the cell"),
		FMath::Abs(SarkoUI::RefusalShakeOffsetPt(0.2f)) <= 4.001f);
	// Clamped, because a curve read one frame past its end must not fling the
	// cell across the panel.
	TestTrue(TEXT("a lerp past the end is clamped, not extrapolated"),
		FMath::IsNearlyZero(SarkoUI::RefusalShakeOffsetPt(1.7f), 0.001f));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
