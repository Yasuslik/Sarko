#include "Misc/AutomationTest.h"

#include "Loot/SarkoItemCatalog.h"
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
	// A 44 pt cell with 4 pt padding is about seven Cyrillic glyphs wide at
	// 8.5 pt, and items.json has no short name. Deriving one is cheaper than a
	// schema field and cannot drift from the catalog, because there is nothing
	// to keep in step.
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

#endif // WITH_AUTOMATION_TESTS
