#include "Misc/AutomationTest.h"

#include "Loot/SarkoEquipment.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/SarkoBackendClient.h"
#include "Shelter/SarkoShelterView.h"

#if WITH_AUTOMATION_TESTS

/**
 * ВИЛАЗКА, the client's half (spec §4.5).
 *
 * Everything asserted here is a DISPLAY or a REQUEST. There is deliberately nothing
 * to test about the kit's contents, the cooldown's length or whether a free run is
 * allowed, because this side decides none of them — the tests for those live in
 * sarko-api/internal/{domain,store,api}. What this file holds is the other half of
 * that boundary: that the client asks for a sortie and nothing more, that it reads
 * back what it is told, and that it cannot draw a conclusion the server did not send.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSortieStartBodyAsksForTheModeAndNothingElse,
	"Sarko.Sortie.StartBodyAsksForTheModeAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSortieStartBodyAsksForTheModeAndNothingElse::RunTest(const FString& Parameters)
{
	const TArray<FSarkoItemStack> Loadout = { FSarkoItemStack{ FName(TEXT("pistol")), 1 } };

	// An ordinary raid's body is BYTE-IDENTICAL to the one this client has always
	// sent. The sortie must not have changed the shape of the request that costs the
	// player their stash.
	const FString Raid = SarkoBackend::MakeRaidStartBody(TEXT("bridge"), Loadout);
	TestEqual(TEXT("a raid's body is unchanged"), Raid,
		FString(TEXT(R"({"map_id":"bridge","loadout":[{"item_id":"pistol","quantity":1}]})")));
	TestFalse(TEXT("and carries no mode field at all"), Raid.Contains(TEXT("mode")));

	// A sortie adds ONE field. This is the whole of what the client gets to say about
	// a free run — and the test is written as an equality rather than a Contains so
	// that a kit field, a cooldown claim or a "free":true could not be added without
	// failing here.
	const FString Sortie = SarkoBackend::MakeRaidStartBody(TEXT("bridge"), Loadout, TEXT("sortie"));
	TestEqual(TEXT("a sortie's body is the raid's plus one field"), Sortie,
		FString(TEXT(R"({"map_id":"bridge","loadout":[{"item_id":"pistol","quantity":1}],"mode":"sortie"})")));
	TestFalse(TEXT("the client never names a kit"), Sortie.Contains(TEXT("kit")));
	TestFalse(TEXT("and never names a cooldown"), Sortie.Contains(TEXT("cooldown")));

	// The loadout is still SENT on a sortie, and the server discards it unread.
	// Suppressing it here would make "a sortie is free" depend on this client's good
	// behaviour, which is exactly the thing a trust boundary must not do.
	TestTrue(TEXT("a sortie still sends whatever the loadout was"),
		Sortie.Contains(TEXT("\"item_id\":\"pistol\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSortieStartResponseReadsTheGrantedKit,
	"Sarko.Sortie.StartResponseReadsTheGrantedKit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSortieStartResponseReadsTheGrantedKit::RunTest(const FString& Parameters)
{
	const FString Head = TEXT(R"("session_id":"s","session_token":"t","seed":7,)"
		R"("expires_at":"2026-08-03T12:00:00Z")");

	FSarkoRaidSession Session;
	FString Error;

	// A sortie: the mode is echoed and the kit comes with it.
	const FString Granted = FString::Printf(
		TEXT("{%s,\"mode\":\"sortie\",\"granted_kit\":[{\"item_id\":\"pistol\",\"quantity\":1},")
		TEXT("{\"item_id\":\"ammo_9mm\",\"quantity\":10}]}"), *Head);
	if (TestTrue(TEXT("a sortie's start response parses"),
		SarkoBackend::ParseRaidStartResponse(Granted, Session, Error)))
	{
		TestTrue(TEXT("and reads as a sortie"), Session.IsSortie());
		TestEqual(TEXT("the whole kit is read"), Session.GrantedKit.Num(), 2);
		if (Session.GrantedKit.Num() == 2)
		{
			TestEqual(TEXT("the gun"), Session.GrantedKit[0].Item, FName(TEXT("pistol")));
			TestEqual(TEXT("and the rounds, with their count"), Session.GrantedKit[1].Quantity, 10);
		}
	}

	// An ordinary raid: `mode` present, `granted_kit` absent (it is omitempty), and
	// that absence is not an error.
	const FString Ordinary = FString::Printf(TEXT("{%s,\"mode\":\"raid\"}"), *Head);
	if (TestTrue(TEXT("a raid's start response parses"),
		SarkoBackend::ParseRaidStartResponse(Ordinary, Session, Error)))
	{
		TestFalse(TEXT("and does not read as a sortie"), Session.IsSortie());
		TestEqual(TEXT("with no borrowed gear"), Session.GrantedKit.Num(), 0);
	}

	// A service OLDER than spec §4.5 sends neither field. It must still parse — this
	// is the offline-degradation discipline — and it must read as "not a sortie",
	// which is the direction that shows no borrowed gear rather than inventing some.
	const FString Older = FString::Printf(TEXT("{%s}"), *Head);
	if (TestTrue(TEXT("a response with no mode at all still parses"),
		SarkoBackend::ParseRaidStartResponse(Older, Session, Error)))
	{
		TestFalse(TEXT("and is treated as an ordinary raid"), Session.IsSortie());
	}

	// A MALFORMED kit fails the whole parse rather than being quietly shortened. The
	// list is what the shelter shows the player they were lent, and a screen
	// understating the server's grant is a screen the player will not trust the next
	// time it shows one.
	const FString Broken = FString::Printf(
		TEXT("{%s,\"mode\":\"sortie\",\"granted_kit\":[{\"item_id\":\"pistol\",\"quantity\":0}]}"), *Head);
	TestFalse(TEXT("a kit row with no positive quantity fails the parse"),
		SarkoBackend::ParseRaidStartResponse(Broken, Session, Error));
	TestTrue(TEXT("and says why"), !Error.IsEmpty());
	TestTrue(TEXT("and leaves nothing half-filled"), Session.SessionId.IsEmpty());

	const FString NotAnArray = FString::Printf(
		TEXT("{%s,\"mode\":\"sortie\",\"granted_kit\":\"a pistol\"}"), *Head);
	TestFalse(TEXT("a granted_kit that is not an array fails the parse"),
		SarkoBackend::ParseRaidStartResponse(NotAnArray, Session, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSortieProfileReadsTheCooldown,
	"Sarko.Sortie.ProfileReadsTheCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSortieProfileReadsTheCooldown::RunTest(const FString& Parameters)
{
	FSarkoProfile Profile;
	FString Error;

	const FString WithCooldown =
		TEXT(R"({"player_id":"p","vehicle_tier":"none","sortie_cooldown_seconds":272})");
	if (TestTrue(TEXT("a profile with a cooldown parses"),
		SarkoBackend::ParseProfileResponse(WithCooldown, Profile, Error)))
	{
		TestEqual(TEXT("the seconds are read"), Profile.SortieCooldownSeconds, 272);
	}

	// ABSENT is zero, i.e. available. That offers a button the server may refuse by
	// name, which costs one round trip; the other direction would hide the recovery
	// path from a player whose service is a version behind.
	const FString Older = TEXT(R"({"player_id":"p","vehicle_tier":"none"})");
	if (TestTrue(TEXT("a profile without one still parses"),
		SarkoBackend::ParseProfileResponse(Older, Profile, Error)))
	{
		TestEqual(TEXT("and the sortie reads as available"), Profile.SortieCooldownSeconds, 0);
	}

	// CLAMPED, not trusted. This number reaches a label without passing through any
	// rule that could have noticed, and a negative would draw a countdown running
	// backwards.
	const FString Negative =
		TEXT(R"({"player_id":"p","vehicle_tier":"none","sortie_cooldown_seconds":-90})");
	if (TestTrue(TEXT("a negative cooldown still parses"),
		SarkoBackend::ParseProfileResponse(Negative, Profile, Error)))
	{
		TestEqual(TEXT("and is clamped to available"), Profile.SortieCooldownSeconds, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSortieButtonShowsTheWordOrTheCountdown,
	"Sarko.Sortie.ButtonShowsTheWordOrTheCountdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSortieButtonShowsTheWordOrTheCountdown::RunTest(const FString& Parameters)
{
	// The formatter first, because both branches below print through it.
	TestEqual(TEXT("minutes and seconds"), SarkoShelter::FormatSortieCooldown(272), FString(TEXT("4:32")));
	TestEqual(TEXT("seconds are zero-padded"), SarkoShelter::FormatSortieCooldown(9), FString(TEXT("0:09")));
	TestEqual(TEXT("a whole minute"), SarkoShelter::FormatSortieCooldown(60), FString(TEXT("1:00")));
	// Never an empty string: a caller that asked for a countdown must get characters,
	// or the button silently loses its label.
	TestEqual(TEXT("zero still prints"), SarkoShelter::FormatSortieCooldown(0), FString(TEXT("0:00")));
	TestEqual(TEXT("and so does a negative"), SarkoShelter::FormatSortieCooldown(-5), FString(TEXT("0:00")));

	FSarkoEquipment Armed;
	Armed.Weapon = FName(TEXT("pistol"));

	// AVAILABLE: the word, the free-of-charge line, and a button that can be pressed.
	const FSarkoRaidButtonView Ready =
		SarkoShelter::BuildRaidButton(Armed, /*bProfileLoaded*/ true, FString(), /*Cooldown*/ 0);
	TestEqual(TEXT("the label is the word"), Ready.SortieLabel, FString(TEXT("ВИЛАЗКА")));
	TestEqual(TEXT("and it says it costs nothing"), Ready.SortieSubLabel, FString(TEXT("БЕЗКОШТОВНО")));
	TestTrue(TEXT("and it can be pressed"), Ready.bSortieEnabled);
	TestFalse(TEXT("and is not on cooldown"), Ready.bSortieOnCooldown);

	// ON COOLDOWN: the label IS the remaining time (spec §4.5 — "showing either
	// ВИЛАЗКА or the cooldown remaining"), and the sub-label goes away because the
	// label is already the whole message.
	const FSarkoRaidButtonView Waiting =
		SarkoShelter::BuildRaidButton(Armed, true, FString(), 272);
	TestEqual(TEXT("the label becomes the countdown"), Waiting.SortieLabel, FString(TEXT("4:32")));
	// And the second line NAMES the button, because a frame showed a greyed "4:32"
	// above В РЕЙД with nothing saying what it was counting towards.
	TestEqual(TEXT("and the second line names what is counting down"),
		Waiting.SortieSubLabel, FString(TEXT("ВИЛАЗКА")));
	TestFalse(TEXT("and the button is refused"), Waiting.bSortieEnabled);
	TestTrue(TEXT("and it knows why"), Waiting.bSortieOnCooldown);

	// THE DEAD-END GUARD IS UNTOUCHED, and this is the assertion that matters most on
	// this screen: the cooldown must never reach В РЕЙД. A player who lost everything
	// and is inside the sortie's cooldown has to still be able to raid unarmed, or the
	// free run has made the game MORE dead-endable than it was before.
	FSarkoEquipment Nothing;
	for (int32 Cooldown : { 0, 1, 272, 100000 })
	{
		const FSarkoRaidButtonView View =
			SarkoShelter::BuildRaidButton(Nothing, /*bProfileLoaded*/ true, FString(), Cooldown);
		TestTrue(TEXT("В РЕЙД is never disabled by the sortie's cooldown"), View.bEnabled);
		TestEqual(TEXT("and the verb never changes"), View.Label, FString(TEXT("В РЕЙД")));
		TestEqual(TEXT("and still says БЕЗ ЗБРОЇ"), View.SubLabel, FString(TEXT("БЕЗ ЗБРОЇ")));
	}

	// Before the first profile the cooldown is UNKNOWN, so the sortie is refused —
	// unlike В РЕЙД, which is refused then too but for a different reason and only
	// then. An OFFLINE shelter refuses it as well: a sortie without the server that
	// grants the kit would be a raid with nothing in it, whereas В РЕЙД must still
	// work (spec §4.6).
	const FSarkoRaidButtonView Loading =
		SarkoShelter::BuildRaidButton(Armed, /*bProfileLoaded*/ false, FString(), 0);
	TestFalse(TEXT("no sortie before the profile lands"), Loading.bSortieEnabled);
	const FSarkoRaidButtonView Offline =
		SarkoShelter::BuildRaidButton(Armed, /*bProfileLoaded*/ false, TEXT("/v1/profile: HTTP 500"), 0);
	TestFalse(TEXT("and none while offline"), Offline.bSortieEnabled);
	TestTrue(TEXT("but an offline shelter still raids"), Offline.bEnabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSortieBorrowedKitFillsTheCharacterPanel,
	"Sarko.Sortie.BorrowedKitFillsTheCharacterPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSortieBorrowedKitFillsTheCharacterPanel::RunTest(const FString& Parameters)
{
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	if (!TestTrue(TEXT("the shipped catalog loaded"), Catalog.Items.Num() > 0))
	{
		return false;
	}

	// The richest authored kit's shape: a gun, a bag, a coat and things to use.
	const TArray<FSarkoItemStack> Kit = {
		FSarkoItemStack{ FName(TEXT("pistol")), 1 },
		FSarkoItemStack{ FName(TEXT("ammo_9mm")), 20 },
		FSarkoItemStack{ FName(TEXT("backpack")), 1 },
		FSarkoItemStack{ FName(TEXT("jacket")), 1 },
		FSarkoItemStack{ FName(TEXT("medkit")), 1 },
	};

	const FSarkoCharacterView Borrowed = SarkoShelter::BuildBorrowedCharacterView(Kit, Catalog);
	TestTrue(TEXT("the panel knows the gear is on loan"), Borrowed.bBorrowed);
	// SAID IN WORDS, not only coloured: "is this mine" is the one question on this
	// screen that must not be carried by a hue.
	TestTrue(TEXT("and says so in the heading"), Borrowed.Title.Contains(TEXT("ПОЗИЧЕНЕ")));
	TestTrue(TEXT("and states the promise the mechanic rests on"),
		Borrowed.PocketsNote.Contains(TEXT("ТВОЄ")));

	// Every worn item is in its authored slot, and the consumables are in none:
	// ammunition and a medkit are what the raid gives you to USE, and putting them on
	// the character's chest would be the panel inventing a slot.
	if (TestEqual(TEXT("all three slots are drawn, occupied or not"), Borrowed.Slots.Num(), 3))
	{
		for (const FSarkoEquipSlotView& Slot : Borrowed.Slots)
		{
			TestTrue(*FString::Printf(TEXT("the %s slot is filled"), *Slot.Caption), Slot.bOccupied);
		}
	}
	const auto ItemIn = [&Borrowed](ESarkoEquipSlot Slot) -> FName
	{
		for (const FSarkoEquipSlotView& View : Borrowed.Slots)
		{
			if (View.Slot == Slot)
			{
				return View.Stack.Item;
			}
		}
		return NAME_None;
	};
	TestEqual(TEXT("the gun is in the hands"), ItemIn(ESarkoEquipSlot::Weapon), FName(TEXT("pistol")));
	TestEqual(TEXT("the bag on the back"), ItemIn(ESarkoEquipSlot::Backpack), FName(TEXT("backpack")));
	TestEqual(TEXT("the coat on the chest"), ItemIn(ESarkoEquipSlot::Clothing), FName(TEXT("jacket")));

	// The floor kit — a gun and rounds, no bag, no coat — draws two EMPTY slots rather
	// than a panel that changes shape. An absent slot would make the body shrink as
	// the roll got worse.
	const TArray<FSarkoItemStack> Floor = {
		FSarkoItemStack{ FName(TEXT("pistol")), 1 },
		FSarkoItemStack{ FName(TEXT("ammo_9mm")), 10 },
	};
	const FSarkoCharacterView Worn = SarkoShelter::BuildBorrowedCharacterView(Floor, Catalog);
	TestEqual(TEXT("the body keeps its shape on a poor roll"), Worn.Slots.Num(), 3);
	TestEqual(TEXT("with the gun in it"), ItemIn(ESarkoEquipSlot::Weapon), FName(TEXT("pistol")));

	// ---- and through the whole view, which is what the widget reads --------------
	//
	// The panel shows the BORROWED kit and not what the player owns, because a sortie
	// carries nothing of theirs. Showing the pistol they own beside a free run that
	// cannot lose it would be the screen promising the wrong stake.
	FSarkoProfile Profile;
	Profile.PlayerId = TEXT("p");
	Profile.VehicleTier = TEXT("none");
	Profile.Equipment.Weapon = FName(TEXT("toolbox")); // deliberately not the kit's gun

	const FSarkoShelterView WithKit = SarkoShelter::BuildView(FSarkoLastRaid(), Profile,
		/*bProfileLoaded*/ true, FString(), FString(), Catalog, ESarkoShelterScreen::Inventory, Kit);
	TestTrue(TEXT("the view draws the borrowed kit"), WithKit.Character.bBorrowed);

	// And with no sortie in flight it is the player's own equipment again, unchanged.
	const FSarkoShelterView Owned = SarkoShelter::BuildView(FSarkoLastRaid(), Profile,
		true, FString(), FString(), Catalog, ESarkoShelterScreen::Inventory);
	TestFalse(TEXT("and without one it is what the player owns"), Owned.Character.bBorrowed);
	TestTrue(TEXT("with the heading it has always had"), Owned.Character.Title == TEXT("ХОДОК"));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
