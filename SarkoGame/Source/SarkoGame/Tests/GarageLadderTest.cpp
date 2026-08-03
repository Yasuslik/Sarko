#include "Misc/AutomationTest.h"

#include "Loot/SarkoItemCatalog.h"
#include "Shelter/SarkoShelterView.h"
#include "UI/SarkoCellWidgets.h"
#include "UI/SarkoInventoryPanel.h"

#if WITH_AUTOMATION_TESTS

/**
 * The two layout flaws the equipment pass left behind, as properties.
 *
 * Neither is a bug in a rule — both are frames that came back wrong — so what is
 * testable about them is the pure function each fix put underneath the drawing.
 * The frames are still the verdict; these stop the same frame coming back twice.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoStashViewportFallsOnAWholeRow,
	"Sarko.UI.StashViewportFallsOnAWholeRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoStashViewportFallsOnAWholeRow::RunTest(const FString& Parameters)
{
	// THE FLAW: the stash grid is a scroll box in a fill-height slot, so the fold
	// landed wherever a 22 pt title and a footer's padding happened to leave it — at
	// 253 pt, five rows and a quarter. The bottom row of a full stash drew as a strip
	// of half-cells, which does not read as "scroll for more"; it reads as a grid that
	// failed to draw, and a player looking at half a РЮКЗАК cannot tell whether they
	// own it.
	const float Pitch = SarkoUI::CellSizePt + SarkoUI::CellGutterPt;
	const auto RowsIn = [Pitch](float Height) { return FMath::RoundToInt((Height + SarkoUI::CellGutterPt) / Pitch); };

	// The measured case that produced the bug. It must come back as five whole rows,
	// not five and a quarter.
	const float Quantised = SarkoUI::WholeRowsHeightPt(253.f);
	TestEqual(TEXT("253 pt is five whole rows"), RowsIn(Quantised), 5);
	TestTrue(TEXT("and the quantised height fits inside what was available"), Quantised <= 253.f);

	// EXACT fits must not lose a row. n rows measure n*Pitch − Gutter, because the
	// gutter lives BETWEEN rows, and a quantiser that divided by the pitch alone would
	// answer n−1 for a viewport built for exactly n.
	for (int32 Rows = 1; Rows <= 8; ++Rows)
	{
		const float Exact = Rows * Pitch - SarkoUI::CellGutterPt;
		TestEqual(*FString::Printf(TEXT("a viewport of exactly %d row(s) keeps them all"), Rows),
			SarkoUI::WholeRowsHeightPt(Exact), Exact);
	}

	// One point short of the next row still answers the row below it, never above:
	// overstating would put the fold back inside a cell, which is the whole flaw.
	for (int32 Rows = 1; Rows <= 8; ++Rows)
	{
		const float JustShort = (Rows + 1) * Pitch - SarkoUI::CellGutterPt - 1.f;
		const float Answer = SarkoUI::WholeRowsHeightPt(JustShort);
		TestTrue(*FString::Printf(TEXT("%.0f pt never claims more room than it has"), JustShort),
			Answer <= JustShort);
		TestEqual(*FString::Printf(TEXT("%.0f pt is %d whole row(s)"), JustShort, Rows),
			RowsIn(Answer), Rows);
	}

	// Under one row, and unmeasured, answer ZERO — which is the caller's signal to
	// leave the height override unset. A height of nought would hide the stash
	// outright, and "not measured yet" has to degrade to the unquantised box rather
	// than to an empty column.
	TestEqual(TEXT("an unmeasured region answers zero"), SarkoUI::WholeRowsHeightPt(0.f), 0.f);
	TestEqual(TEXT("and so does a negative one"), SarkoUI::WholeRowsHeightPt(-40.f), 0.f);
	TestEqual(TEXT("and a region too short for one row"),
		SarkoUI::WholeRowsHeightPt(SarkoUI::CellSizePt - 1.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoTheLadderNamesEachRungsStateInWords,
	"Sarko.Shelter.TheLadderNamesEachRungsStateInWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoTheLadderNamesEachRungsStateInWords::RunTest(const FString& Parameters)
{
	// THE FLAW: ГАРАЖ's lower half was empty, because the ladder was four lines of
	// text in a scroll box — and the state of each rung was carried ONLY by a colour
	// and a one-character marker. Enough to tell three rungs apart, not enough to
	// explain them: three greys cannot say which vehicle is merely not yet and which
	// one this build can never produce, and those are different disappointments. A
	// player who cannot tell will spend raids looking for an engine that is not in
	// domain.ItemDefs at all.
	//
	// The layout is the frame's business. What is testable is that every rung carries
	// a word, that the word distinguishes the two kinds of unavailability, and that
	// the screen says why out loud.
	const TArray<FSarkoVehicleRung> None = SarkoShelter::VehicleLadder(TEXT("none"));
	if (!TestEqual(TEXT("four rungs"), None.Num(), 4))
	{
		return false;
	}
	for (const FSarkoVehicleRung& Rung : None)
	{
		TestFalse(*FString::Printf(TEXT("'%s' states itself in words"), *Rung.Text),
			Rung.StateText.IsEmpty());
	}

	// ONLY the bicycle is craftable in this build, and that is a fact about the
	// backend: the later tiers' parts are deliberately absent from domain.ItemDefs, so
	// no loot table can yield them and no craft can ever succeed.
	TestTrue(TEXT("the bicycle can be built"), None[0].bCraftable);
	TestEqual(TEXT("and is what is next"), None[1 - 1].StateText, FString(TEXT("НАСТУПНИЙ")));
	for (int32 Index = 1; Index < None.Num(); ++Index)
	{
		TestFalse(*FString::Printf(TEXT("rung %d cannot be built in this build"), Index),
			None[Index].bCraftable);
		TestEqual(*FString::Printf(TEXT("and rung %d says why"), Index),
			None[Index].StateText, FString(TEXT("ДЕТАЛІ НЕ В ЗОНІ")));
	}

	// A built rung says so, and in the same word wherever it appears.
	const TArray<FSarkoVehicleRung> Rider = SarkoShelter::VehicleLadder(TEXT("bicycle"));
	TestEqual(TEXT("a built rung is ЗІБРАНО"), Rider[0].StateText, FString(TEXT("ЗІБРАНО")));
	// The motorcycle is `next` AND uncraftable, and the uncraftable answer wins: being
	// next is no use to a player who cannot get the parts, and telling them
	// "НАСТУПНИЙ" would send them looking.
	TestTrue(TEXT("the motorcycle is next"), Rider[1].bNext);
	TestEqual(TEXT("but says the truer thing"), Rider[1].StateText, FString(TEXT("ДЕТАЛІ НЕ В ЗОНІ")));

	// And the whole-ladder explanation reaches the view, because a greyed rung with no
	// reason is the failure the craft button's "НЕ ВИСТАЧАЄ: ..." label exists to
	// avoid, one column over.
	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	const FSarkoShelterView View = SarkoShelter::BuildView(FSarkoLastRaid(), Profile, true,
		FString(), FString(), SarkoLoot::GetItemCatalog(), ESarkoShelterScreen::Garage);
	TestFalse(TEXT("the screen says why the upper rungs are grey"), View.Garage.LadderNote.IsEmpty());
	TestTrue(TEXT("and names the zone rather than blaming the player"),
		View.Garage.LadderNote.Contains(TEXT("ЗОНІ")));

	// It is dropped once there is nothing left to explain — a note that outlives its
	// reason teaches the wrong thing. Nothing today reaches that state, which is why
	// the property is asserted through the predicate rather than through a tier.
	TestTrue(TEXT("the note exists exactly when an unreachable rung does"),
		View.Garage.LadderNote.IsEmpty() == !View.Garage.Ladder.ContainsByPredicate(
			[](const FSarkoVehicleRung& Rung) { return !Rung.bBuilt && !Rung.bCraftable; }));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
