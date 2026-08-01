#include "Misc/AutomationTest.h"

#include "AI/SarkoBotArchetypes.h"
#include "Core/SarkoEncounters.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEncounterTriggerArming,
	"Sarko.Encounter.TriggerArming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEncounterTriggerArming::RunTest(const FString& Parameters)
{
	using namespace SarkoEncounter;

	const float RadiusUU = 2600.f;
	const float ArmAfterUU = 3400.f;

	TestTrue(TEXT("walking into the radius arms an unfired trigger"),
		ShouldArm(/*bFired*/ false, /*bOneShot*/ true, /*bBeyondRearm*/ true, 2000.f, RadiusUU));
	TestFalse(TEXT("standing outside the radius does not"),
		ShouldArm(false, true, true, 2700.f, RadiusUU));
	TestTrue(TEXT("the boundary itself counts as inside"),
		ShouldArm(false, true, true, RadiusUU, RadiusUU));

	// One-shot is the tutorial's whole shape: an encounter that has spent its
	// budget is over, and walking back into the POI does not reopen it.
	TestFalse(TEXT("a fired one-shot encounter never arms again"),
		ShouldArm(/*bFired*/ true, /*bOneShot*/ true, true, 100.f, RadiusUU));
	TestTrue(TEXT("a fired re-armable encounter can arm again once the latch is open"),
		ShouldArm(/*bFired*/ true, /*bOneShot*/ false, /*bBeyondRearm*/ true, 100.f, RadiusUU));
	TestFalse(TEXT("but not while the latch is still closed"),
		ShouldArm(true, false, /*bBeyondRearm*/ false, 100.f, RadiusUU));

	// THE HYSTERESIS. armAfterUU exceeds radiusUU, so the band between them is
	// where a player loitering on the boundary would otherwise pump the system:
	// in, armed; out, disarmed; in, armed — several times a second.
	TestFalse(TEXT("a closed latch stays closed inside the radius"),
		UpdateBeyondRearm(/*bWasBeyond*/ false, 1000.f, ArmAfterUU));
	TestFalse(TEXT("and stays closed in the hysteresis band, which is the whole point"),
		UpdateBeyondRearm(false, (RadiusUU + ArmAfterUU) * 0.5f, ArmAfterUU));
	TestTrue(TEXT("only going beyond armAfterUU reopens it"),
		UpdateBeyondRearm(false, ArmAfterUU + 1.f, ArmAfterUU));
	TestTrue(TEXT("an open latch stays open while the player closes in"),
		UpdateBeyondRearm(/*bWasBeyond*/ true, 0.f, ArmAfterUU));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEncounterBudgetIsTheLaw,
	"Sarko.Encounter.BudgetIsTheLaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEncounterBudgetIsTheLaw::RunTest(const FString& Parameters)
{
	using namespace SarkoEncounter;

	const int32 FirstFightMaxAlive = 1;

	// The tutorial, played out as arithmetic: budget 4, three encounters costing
	// 1 + 1 + 2. The first fight is one enemy because the SYSTEM caps it, not
	// because the authoring happens to.
	int32 Budget = 4;
	const int32 Gas = AllowedSpawnCount(Budget, /*Cost*/ 1, /*MaxAlive*/ 1, /*AliveNow*/ 0,
		/*AuthoredPoints*/ 2, /*bFirstFight*/ true, FirstFightMaxAlive);
	TestEqual(TEXT("the gas station spawns exactly one"), Gas, 1);
	Budget -= 1;

	const int32 Depot = AllowedSpawnCount(Budget, 1, 1, 0, 2, /*bFirstFight*/ false, FirstFightMaxAlive);
	TestEqual(TEXT("the depot approach spawns exactly one"), Depot, 1);
	Budget -= 1;

	const int32 Warehouse = AllowedSpawnCount(Budget, 2, 2, 0, 3, false, FirstFightMaxAlive);
	TestEqual(TEXT("the warehouse spawns two"), Warehouse, 2);
	Budget -= 2;

	TestEqual(TEXT("the tutorial's budget is spent exactly"), Budget, 0);
	TestEqual(TEXT("and a fourth encounter of any size spawns nothing"),
		AllowedSpawnCount(Budget, 1, 2, 0, 4, false, FirstFightMaxAlive), 0);

	// A two-bot event that does not fit does not happen AT ALL — not as one bot.
	// A warehouse fight with half its enemies is not the event that was
	// authored, and in a raid with four enemies in total a half-event is worse
	// than none.
	TestEqual(TEXT("an encounter costing 2 with 1 left spawns nothing, not one"),
		AllowedSpawnCount(/*BudgetRemaining*/ 1, /*Cost*/ 2, /*MaxAlive*/ 2, 0, 3, false, FirstFightMaxAlive), 0);

	// The first-fight cap outranks a generous encounter.
	TestEqual(TEXT("a 2-bot encounter that happens to be the first fight still spawns one"),
		AllowedSpawnCount(8, 2, 2, 0, 3, /*bFirstFight*/ true, FirstFightMaxAlive), 1);

	// maxAlive is per encounter and counts what is already standing.
	TestEqual(TEXT("an encounter at its own ceiling spawns nothing more"),
		AllowedSpawnCount(8, 2, 2, /*AliveNow*/ 2, 3, false, FirstFightMaxAlive), 0);
	TestEqual(TEXT("and tops up to the ceiling when one of its bots is dead"),
		AllowedSpawnCount(8, 2, 2, /*AliveNow*/ 1, 3, false, FirstFightMaxAlive), 1);

	// It can never ask for more doors than the map authored.
	TestEqual(TEXT("never more enemies than there are authored spawn points"),
		AllowedSpawnCount(8, 3, 3, 0, /*AuthoredPoints*/ 2, false, FirstFightMaxAlive), 2);

	// The normal raid's ladder, for the same shape at a different ceiling.
	TestEqual(TEXT("a normal raid's budget of 8 admits a 3-bot event"),
		AllowedSpawnCount(8, 3, 3, 0, 4, false, 2), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoEncounterSpawnPlacement,
	"Sarko.Encounter.SpawnPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoEncounterSpawnPlacement::RunTest(const FString& Parameters)
{
	using namespace SarkoEncounter;

	// 1800 uu is measured, not picked: the portrait camera (a 1400 uu boom at
	// -70 degrees) shows about 1380 uu ahead of the pawn and 545 uu to either
	// side, so this is beyond the forward reach with margin.
	const float MinDistanceUU = 1800.f;

	TestTrue(TEXT("far away and out of sight qualifies"),
		SpawnPointQualifies(2400.f, /*bSeesPlayer*/ false, MinDistanceUU));

	// Both conditions, and the two failures below are why it is AND and not OR.
	TestFalse(TEXT("far away but in plain sight does NOT — 1800 uu up-screen on open ground is a visible dot"),
		SpawnPointQualifies(2400.f, /*bSeesPlayer*/ true, MinDistanceUU));
	TestFalse(TEXT("out of sight but close does NOT — a bot appearing behind the crate you are looting is worse"),
		SpawnPointQualifies(400.f, false, MinDistanceUU));
	TestFalse(TEXT("close and visible certainly does not"),
		SpawnPointQualifies(400.f, true, MinDistanceUU));
	TestTrue(TEXT("exactly at the floor qualifies"),
		SpawnPointQualifies(MinDistanceUU, false, MinDistanceUU));
	TestFalse(TEXT("a hair inside it does not"),
		SpawnPointQualifies(MinDistanceUU - 1.f, false, MinDistanceUU));

	// Deferral: the system never relocates a spawn toward the player and never
	// spawns in view, so waiting is the only move it has — but a wait with no
	// end is an encounter silently armed for the rest of the raid.
	const float MaxDeferSeconds = 5.f;
	TestFalse(TEXT("a fresh deferral keeps waiting"), ShouldAbandonDeferral(0.f, MaxDeferSeconds));
	TestFalse(TEXT("four seconds of deferral keeps waiting"), ShouldAbandonDeferral(4.f, MaxDeferSeconds));
	TestTrue(TEXT("five seconds gives the attempt up"), ShouldAbandonDeferral(5.f, MaxDeferSeconds));
	TestTrue(TEXT("and so does anything past it"), ShouldAbandonDeferral(20.f, MaxDeferSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBotArchetypeTable,
	"Sarko.Encounter.BotArchetypeTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBotArchetypeTable::RunTest(const FString& Parameters)
{
	// The three names bridge.json is allowed to write, and the two properties
	// that make the table worth having rather than three copies of one row.
	FSarkoBotArchetype Archetype;
	for (const TCHAR* Id : { TEXT("scav_pistol"), TEXT("scav_smg"), TEXT("scout") })
	{
		TestTrue(FString::Printf(TEXT("'%s' is in the table"), Id),
			SarkoAI::FindBotArchetype(FName(Id), Archetype));
	}
	TestFalse(TEXT("an unknown archetype is not silently defaulted"),
		SarkoAI::FindBotArchetype(TEXT("scav_rocket_launcher"), Archetype));

	// ТЗ §11, as a property of the data: NO archetype may open fire from further
	// than the player can see. The measured forward view is about 1380 uu. This
	// is the one number in the table that is not a taste question, so it is the
	// one the suite pins.
	for (const FSarkoBotArchetype& Row : SarkoAI::GetBotArchetypes())
	{
		TestTrue(FString::Printf(TEXT("'%s' fires from inside the screen (%.0f uu <= 1380)"),
			*Row.Id.ToString(), Row.FiringRangeUU), Row.FiringRangeUU <= 1380.f);
		TestTrue(FString::Printf(TEXT("'%s' hears at least as far as it shoots"), *Row.Id.ToString()),
			Row.HearingRadiusUU >= Row.FiringRangeUU);
		TestTrue(FString::Printf(TEXT("'%s' has a positive health pool, damage and cadence"), *Row.Id.ToString()),
			Row.MaxHealth > 0.f && Row.Damage > 0.f && Row.FireIntervalSeconds > 0.f && Row.WalkSpeed > 0.f);
	}

	// The tutorial's teacher, to the number: 60 hp is four of the player's shots
	// at the realism stage's damage, so the first fight is a magazine and a
	// decision rather than a duel of attrition.
	TestTrue(TEXT("scav_pistol resolves"), SarkoAI::FindBotArchetype(TEXT("scav_pistol"), Archetype));
	TestEqual(TEXT("scav_pistol has 60 hp"), Archetype.MaxHealth, 60.f);
	TestEqual(TEXT("scav_pistol fires at 1100 uu"), Archetype.FiringRangeUU, 1100.f);

	// Guard against the table collapsing into three identical rows, which would
	// pass everything above and make the archetype concept decorative.
	TSet<float> DistinctSpeeds;
	for (const FSarkoBotArchetype& Row : SarkoAI::GetBotArchetypes())
	{
		DistinctSpeeds.Add(Row.WalkSpeed);
	}
	TestEqual(TEXT("the three archetypes actually differ"), DistinctSpeeds.Num(), SarkoAI::GetBotArchetypes().Num());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
