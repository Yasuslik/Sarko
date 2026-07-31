#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Dom/JsonObject.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/SarkoBackendClient.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackendBodiesMatchTheContract,
	"Sarko.Backend.BodiesMatchTheContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackendBodiesMatchTheContract::RunTest(const FString& Parameters)
{
	// Every field name below is copied from sarko-api/internal/api's request
	// structs. A renamed field is not a compile error on either side — it is a
	// 400 fifteen minutes into a raid, with the haul on the line.

	const FString Anonymous = SarkoBackend::MakeAnonymousBody(TEXT("device-abc"));
	TestTrue(TEXT("anonymous body carries device_id"), Anonymous.Contains(TEXT("\"device_id\"")));
	TestTrue(TEXT("anonymous body carries the value"), Anonymous.Contains(TEXT("device-abc")));

	TArray<FSarkoItemStack> Loadout;
	Loadout.Add(FSarkoItemStack{ TEXT("pistol"), 1 });
	Loadout.Add(FSarkoItemStack{ TEXT("ammo_9mm"), 60 });
	const FString Start = SarkoBackend::MakeRaidStartBody(TEXT("bridge"), Loadout);
	TestTrue(TEXT("start body carries map_id"), Start.Contains(TEXT("\"map_id\"")));
	TestTrue(TEXT("start body carries the map"), Start.Contains(TEXT("bridge")));
	TestTrue(TEXT("start body carries loadout"), Start.Contains(TEXT("\"loadout\"")));
	TestTrue(TEXT("stacks use item_id, not id or item"), Start.Contains(TEXT("\"item_id\"")));
	TestTrue(TEXT("stacks use quantity, not qty or count"), Start.Contains(TEXT("\"quantity\"")));
	TestFalse(TEXT("no stray 'item' key"), Start.Contains(TEXT("\"item\":")));

	// An empty loadout is legal (domain.ValidateStacks allows it) and must still
	// produce an array, not a null.
	const FString EmptyStart = SarkoBackend::MakeRaidStartBody(TEXT("bridge"), TArray<FSarkoItemStack>());
	TestTrue(TEXT("an empty loadout is an empty array"), EmptyStart.Contains(TEXT("\"loadout\":[]")));

	const FString Session = SarkoBackend::MakeSessionBody(TEXT("sid"), TEXT("stok"));
	TestTrue(TEXT("session body carries session_id"), Session.Contains(TEXT("\"session_id\"")));
	TestTrue(TEXT("session body carries session_token"), Session.Contains(TEXT("\"session_token\"")));

	TArray<FSarkoItemStack> Haul;
	Haul.Add(FSarkoItemStack{ TEXT("scrap_metal"), 4 });
	const FString Result = SarkoBackend::MakeRaidResultBody(TEXT("sid"), TEXT("stok"), TEXT("extracted"), Haul);
	TestTrue(TEXT("result body carries session_id"), Result.Contains(TEXT("\"session_id\"")));
	TestTrue(TEXT("result body carries session_token"), Result.Contains(TEXT("\"session_token\"")));
	TestTrue(TEXT("result body carries outcome"), Result.Contains(TEXT("\"outcome\":\"extracted\"")));
	TestTrue(TEXT("result body carries items"), Result.Contains(TEXT("\"items\"")));

	// Zero and negative quantities are dropped before they leave: the backend
	// rejects the whole request for one bad stack (domain.ValidateStacks), so a
	// stack that should not exist must not cost the player the rest of the haul.
	TArray<FSarkoItemStack> Dirty;
	Dirty.Add(FSarkoItemStack{ TEXT("scrap_metal"), 0 });
	Dirty.Add(FSarkoItemStack{ TEXT("vodka"), -2 });
	Dirty.Add(FSarkoItemStack{ TEXT("medkit"), 1 });
	const FString Cleaned = SarkoBackend::MakeRaidResultBody(TEXT("sid"), TEXT("stok"), TEXT("died"), Dirty);
	TestTrue(TEXT("a valid stack survives"), Cleaned.Contains(TEXT("medkit")));
	TestFalse(TEXT("a zero-quantity stack is dropped"), Cleaned.Contains(TEXT("scrap_metal")));
	TestFalse(TEXT("a negative-quantity stack is dropped"), Cleaned.Contains(TEXT("vodka")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackendParsesRealResponses,
	"Sarko.Backend.ParsesRealResponses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackendParsesRealResponses::RunTest(const FString& Parameters)
{
	// These two bodies are verbatim captures from the deployed service on
	// 2026-07-30, not invented shapes.
	const FString AuthJson = TEXT(R"({"player_id":"4ebb53e6-08ef-4709-b0e0-b3a8b6c06ca8","token":"eyJhbGciOiJIUzI1NiJ9.e30.x"})");
	FString PlayerId;
	FString Token;
	FString Error;
	TestTrue(TEXT("the real auth response parses"),
		SarkoBackend::ParseAnonymousResponse(AuthJson, PlayerId, Token, Error));
	TestEqual(TEXT("player_id is read"), PlayerId, FString(TEXT("4ebb53e6-08ef-4709-b0e0-b3a8b6c06ca8")));
	TestFalse(TEXT("token is read"), Token.IsEmpty());

	const FString StartJson = TEXT(R"({"session_id":"62494d7d-9e8f-4248-836c-2de6ac64f87a","session_token":"zLpNBQdn-3W2oTqLXEqUPs3DCH6ODeWbez_IWCSZr_s","seed":3402905197,"expires_at":"2026-07-30T20:46:28.858863Z"})");
	FSarkoRaidSession Session;
	TestTrue(TEXT("the real raid/start response parses"),
		SarkoBackend::ParseRaidStartResponse(StartJson, Session, Error));
	TestEqual(TEXT("session_id is read"), Session.SessionId, FString(TEXT("62494d7d-9e8f-4248-836c-2de6ac64f87a")));
	TestFalse(TEXT("session_token is read"), Session.SessionToken.IsEmpty());
	TestTrue(TEXT("expires_at is read"), Session.ExpiresAt.GetYear() == 2026);

	// The seed the deployed service actually returned. StartRaid does
	// int64(rand.Uint32()), so about half of all seeds exceed INT32_MAX; a naive
	// assignment or FCString::Atoi is silent, platform-dependent corruption, and
	// a corrupted seed means the server and the client disagree about what is in
	// every crate.
	TestEqual(TEXT("a seed above INT32_MAX wraps bit-for-bit"),
		SarkoBackend::SeedToInt32(3402905197LL), static_cast<int32>(static_cast<uint32>(3402905197u)));
	TestEqual(TEXT("and that is negative, not clamped"), Session.Seed, SarkoBackend::SeedToInt32(3402905197LL));
	TestTrue(TEXT("the wrapped seed really is negative"), Session.Seed < 0);
	TestEqual(TEXT("a small seed is unchanged"), SarkoBackend::SeedToInt32(7LL), 7);
	TestEqual(TEXT("the largest uint32 wraps to -1"), SarkoBackend::SeedToInt32(4294967295LL), -1);

	const FString ConfirmJson = TEXT(R"({"expires_at":"2026-07-30T21:00:00Z"})");
	FDateTime ExpiresAt;
	TestTrue(TEXT("the confirm response parses"),
		SarkoBackend::ParseExpiresAtResponse(ConfirmJson, ExpiresAt, Error));
	TestEqual(TEXT("the confirm deadline is read"), ExpiresAt.GetHour(), 21);

	// Errors are always {"error":{"code","message"}}.
	const FString ErrorJson = TEXT(R"({"error":{"code":"map_locked","message":"your garage does not unlock this map"}})");
	FSarkoBackendError Parsed;
	TestTrue(TEXT("an error body parses"), SarkoBackend::ParseErrorResponse(ErrorJson, Parsed));
	TestEqual(TEXT("the code is read"), Parsed.Code, FString(TEXT("map_locked")));
	TestFalse(TEXT("the message is read"), Parsed.Message.IsEmpty());
	TestFalse(TEXT("a success body is not an error"), SarkoBackend::ParseErrorResponse(AuthJson, Parsed));

	// Every code this plan handles by name must survive the round trip. They are
	// matched by string on the way out, so a parser that dropped the code would
	// turn a recoverable 409 into an unexplained failure.
	for (const TCHAR* Code : { TEXT("map_locked"), TEXT("raid_in_progress"), TEXT("insufficient_items"),
		TEXT("session_not_open"), TEXT("bad_session_token"), TEXT("safe_pocket_overflow") })
	{
		const FString Body = FString::Printf(TEXT("{\"error\":{\"code\":\"%s\",\"message\":\"m\"}}"), Code);
		FSarkoBackendError RoundTripped;
		TestTrue(*FString::Printf(TEXT("'%s' parses as an error"), Code),
			SarkoBackend::ParseErrorResponse(Body, RoundTripped));
		TestEqual(*FString::Printf(TEXT("'%s' round-trips by name"), Code), RoundTripped.Code, FString(Code));
	}

	// Malformed input never yields a half-filled session.
	for (const FString& Bad : { FString(TEXT("{{{")), FString(TEXT("{}")),
		FString(TEXT(R"({"session_id":"x"})")), FString(TEXT(R"({"session_id":"x","session_token":"y"})")) })
	{
		FSarkoRaidSession Broken;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Bad),
			SarkoBackend::ParseRaidStartResponse(Bad, Broken, Error));
		TestFalse(TEXT("and names the problem"), Error.IsEmpty());
		TestTrue(TEXT("and leaves nothing usable behind"),
			Broken.SessionId.IsEmpty() && Broken.SessionToken.IsEmpty() && Broken.Seed == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackendSettingsAreShippable,
	"Sarko.Backend.SettingsAreShippable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackendSettingsAreShippable::RunTest(const FString& Parameters)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	// USarkoRaidSettings is config=Game, so these come from DefaultGame.ini. A
	// value put in DefaultEngine.ini by mistake would silently leave the C++
	// default here and nothing would warn.
	TestTrue(TEXT("the base url is https"), Settings.BackendBaseUrl.StartsWith(TEXT("https://")));
	TestFalse(TEXT("the base url has no trailing slash"), Settings.BackendBaseUrl.EndsWith(TEXT("/")));

	// Not just a prefix check: the ini parser swallows "//" as a comment, so an
	// unquoted URL in DefaultGame.ini loads as the string "https:" — which passes
	// a naive scheme test and then sends every request nowhere.
	TestTrue(TEXT("the base url carries a host, not just a scheme"),
		Settings.BackendBaseUrl.Contains(TEXT(".up.railway.app")));

	// The wire map id must be a map the backend actually unlocks at vehicle tier
	// `none`. sarko-api/internal/domain/garage.go maps TierNone to "bridge" since
	// the forest->bridge rename (commit 80a5a4d); sending anything else gets a 403
	// map_locked. It stays a separate setting from MapId so the local data-file
	// name and the wire id can diverge again without a code change.
	TestEqual(TEXT("the wire map id is the tier-none map"), Settings.BackendMapId, FString(TEXT("bridge")));
	TestEqual(TEXT("the local data file is still bridge"), Settings.MapId, FName(TEXT("bridge")));

	// The grace margin exists so the client's clock ends before the server's
	// deadline: RAID_TTL is 20m and GRACE_BUFFER 2m on the deployed service, and
	// sarko-api/README.md is explicit that the buffer is not play time. The margin
	// still has to be here — it is what keeps the clock short of expires_at if the
	// TTL is ever lowered again.
	TestTrue(TEXT("there is a grace margin"), Settings.BackendGraceMarginSeconds >= 60.f);
	TestTrue(TEXT("the HTTP timeout is short enough not to stall a raid"),
		Settings.BackendTimeoutSeconds > 0.f && Settings.BackendTimeoutSeconds <= 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRaidClockStopsShortOfTheServerDeadline,
	"Sarko.Backend.RaidClockStopsShortOfTheServerDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRaidClockStopsShortOfTheServerDeadline::RunTest(const FString& Parameters)
{
	// The clamp: a confirm that hands back a deadline 14 minutes out (the shape
	// RAID_TTL=12m + GRACE_BUFFER=2m produced) must not let a 15-minute map run to
	// the deadline. sarko-api/README.md is explicit that the grace buffer is slack
	// for a slow result submission and not play time, so the clock lands on
	// deadline − margin = 12 minutes, not 14 and not 15.
	//
	// RAID_TTL is 20m on the deployed service today, so in practice the map's own
	// duration wins and this branch never fires — it stays tested because the
	// service's TTL is an environment variable and this is the only thing standing
	// between lowering it again and hauls lost to a clock that outran the session.
	const float Clock = SarkoBackend::ClockSecondsFromDeadline(
		/*MapDurationSeconds*/ 900.f, /*SecondsUntilDeadline*/ 840.0, /*GraceMarginSeconds*/ 120.f);
	TestTrue(TEXT("the clock stops short of the server deadline"), Clock < 840.f);
	TestTrue(TEXT("and equals the deadline minus the grace margin"), FMath::IsNearlyEqual(Clock, 720.f, 0.5f));

	// When the server is generous — which is the live configuration, RAID_TTL=20m —
	// the map's own duration still wins: a 15-minute map must not become a
	// 25-minute raid because RAID_TTL was raised.
	TestTrue(TEXT("the map duration is the ceiling"),
		FMath::IsNearlyEqual(SarkoBackend::ClockSecondsFromDeadline(900.f, 1800.0, 120.f), 900.f, 0.5f));

	// A deadline already in the past, or inside the margin, must not produce a
	// zero or negative clock — that would end the raid on the spawn frame. Against a
	// 15-minute map both of these now route through the clock-skew branch (see
	// RaidClockDistrustsASkewedLocalClock) and come back with the map's duration,
	// which is a stronger result than the 30-second floor and satisfies the same
	// rule; the Warning each one logs is the branch announcing itself.
	TestTrue(TEXT("an expired deadline still yields a playable floor"),
		SarkoBackend::ClockSecondsFromDeadline(900.f, -5.0, 120.f) >= 30.f);
	TestTrue(TEXT("a deadline inside the margin still yields a playable floor"),
		SarkoBackend::ClockSecondsFromDeadline(900.f, 60.0, 120.f) >= 30.f);

	// The floor itself still exists, for the case the skew branch cannot cover: a
	// short map *and* a dead deadline, where there is no believable larger number to
	// fall back to.
	TestTrue(TEXT("a short map with an expired deadline still gets the 30s floor"),
		FMath::IsNearlyEqual(SarkoBackend::ClockSecondsFromDeadline(60.f, -5.0, 120.f), 30.f, 0.5f));

	// A map with no duration falls back to the settings default, never to zero.
	TestTrue(TEXT("a zero map duration does not produce a zero clock"),
		SarkoBackend::ClockSecondsFromDeadline(0.f, 840.0, 120.f) > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoOutcomeMapsToTheWireStrings,
	"Sarko.Backend.OutcomeMapsToTheWireStrings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoOutcomeMapsToTheWireStrings::RunTest(const FString& Parameters)
{
	// domain.IsValidOutcome accepts exactly "extracted" and "died". Anything else
	// is a 400 and a lost raid.
	TestEqual(TEXT("extracted"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::Extracted)), FString(TEXT("extracted")));
	TestEqual(TEXT("died"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::Died)), FString(TEXT("died")));

	// Spec §4.5: MIA is death. There is no third outcome on the wire, and
	// inventing one would be rejected outright.
	TestEqual(TEXT("MIA is submitted as died"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::MIA)), FString(TEXT("died")));

	// InProgress must never be submitted; it maps to died so a bug cannot invent
	// an extraction, which is the direction that would grant loot for free.
	TestEqual(TEXT("InProgress degrades to died, never to extracted"),
		FString(SarkoBackend::OutcomeToWire(ESarkoRaidOutcome::InProgress)), FString(TEXT("died")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoWireLoadoutIsEmptyAndUnpaid,
	"Sarko.Backend.WireLoadoutIsEmptyAndUnpaid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoWireLoadoutIsEmptyAndUnpaid::RunTest(const FString& Parameters)
{
	// The loadout must be empty, and this is the test that says why rather than a
	// test that merely records the current value.
	//
	// /v1/raid/start debits the loadout from the stash and only the raid result
	// credits anything back — and the result submits the backpack alone. So any
	// non-empty loadout here is a withdrawal with no matching deposit: raid 1 spends
	// the starter kit, raid 2 is 409 insufficient_items, and the client degrades
	// offline for the rest of the install's life. The online loop working exactly
	// once is what this guards against.
	const TArray<FSarkoItemStack> Loadout = SarkoBackend::WireLoadout();
	TestEqual(TEXT("the raid takes nothing in until the result can credit it back"), Loadout.Num(), 0);

	// And it must serialise as an empty array rather than a null: domain.
	// ValidateStacks accepts an empty list, but a `"loadout":null` would be a 400.
	const FString Start = SarkoBackend::MakeRaidStartBody(TEXT("bridge"), Loadout);
	TestTrue(TEXT("an empty loadout is an empty JSON array"), Start.Contains(TEXT("\"loadout\":[]")));

	// When it is refilled — once weapons and ammo are real in-raid items — every
	// entry still has to be a catalog item with a positive quantity, or /v1/raid/
	// start answers 400 implausible_items and no raid ever begins. Kept live so the
	// rule is already enforced on the day somebody adds the first stack back.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	for (const FSarkoItemStack& Stack : Loadout)
	{
		TestNotNull(*FString::Printf(TEXT("loadout item '%s' is in the catalog"), *Stack.Item.ToString()),
			Catalog.Find(Stack.Item));
		TestTrue(TEXT("quantities are positive"), Stack.Quantity > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRaidClockDistrustsASkewedLocalClock,
	"Sarko.Backend.RaidClockDistrustsASkewedLocalClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRaidClockDistrustsASkewedLocalClock::RunTest(const FString& Parameters)
{
	// SecondsUntilDeadline is computed from this machine's UtcNow, so a local clock
	// running fast shrinks it with nothing wrong on the server. Before the bound
	// below, a laptop ten minutes ahead floored a 15-minute raid to the 30-second
	// minimum while the log blamed RAID_TTL for it.
	TestTrue(TEXT("a wildly short deadline is treated as skew, not as the raid's length"),
		FMath::IsNearlyEqual(SarkoBackend::ClockSecondsFromDeadline(900.f, 30.0, 120.f), 900.f, 0.5f));
	TestTrue(TEXT("and so is a deadline already in the past"),
		FMath::IsNearlyEqual(SarkoBackend::ClockSecondsFromDeadline(900.f, -600.0, 120.f), 900.f, 0.5f));

	// The honest clamp survives: a genuinely short session (the RAID_TTL=12m shape)
	// still wins over the map, because deadline − margin is a believable raid length.
	TestTrue(TEXT("a believable short deadline is still obeyed"),
		FMath::IsNearlyEqual(SarkoBackend::ClockSecondsFromDeadline(900.f, 840.0, 120.f), 720.f, 0.5f));
	TestTrue(TEXT("a deadline just above the sane bound is still obeyed, not overridden"),
		SarkoBackend::ClockSecondsFromDeadline(900.f, 320.0, 120.f) < 900.f);

	// A short *map* is not skew: a 60-second test map asking for 60 seconds must not
	// be inflated, so the bound only fires when the two numbers disagree wildly.
	TestTrue(TEXT("a genuinely short map is not mistaken for skew"),
		SarkoBackend::ClockSecondsFromDeadline(60.f, 90.0, 30.f) <= 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoBackendBodiesEscapeTheirStrings,
	"Sarko.Backend.BodiesEscapeTheirStrings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoBackendBodiesEscapeTheirStrings::RunTest(const FString& Parameters)
{
	// The bodies are assembled by hand, so every interpolated value has to be
	// escaped by hand too. A hand-edited BackendMapId with a quote in it would
	// otherwise produce malformed JSON and a 400 that nothing in the log explains —
	// the request would look correct and be rejected anyway.
	//
	// Round-tripped through the JSON reader rather than string-matched: the only
	// thing worth asserting is that the backend's parser can read what was built.
	const FString Start = SarkoBackend::MakeRaidStartBody(TEXT("bri\"dge\\x"), TArray<FSarkoItemStack>());
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Start);
	TestTrue(TEXT("a map id with a quote still produces parseable JSON"),
		FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid());
	if (Root.IsValid())
	{
		FString MapId;
		TestTrue(TEXT("and map_id survives the escaping unchanged"),
			Root->TryGetStringField(TEXT("map_id"), MapId));
		TestEqual(TEXT("byte for byte"), MapId, FString(TEXT("bri\"dge\\x")));
	}

	// The session token is the one string that cannot be re-fetched (the backend
	// keeps only its hash), so a body that mangles it loses the raid's result.
	const FString Session = SarkoBackend::MakeSessionBody(TEXT("sid\"1"), TEXT("tok\\en\nnewline"));
	TSharedPtr<FJsonObject> SessionRoot;
	const TSharedRef<TJsonReader<>> SessionReader = TJsonReaderFactory<>::Create(Session);
	TestTrue(TEXT("awkward session strings still produce parseable JSON"),
		FJsonSerializer::Deserialize(SessionReader, SessionRoot) && SessionRoot.IsValid());
	if (SessionRoot.IsValid())
	{
		FString Token;
		SessionRoot->TryGetStringField(TEXT("session_token"), Token);
		TestEqual(TEXT("and the token round-trips byte for byte"), Token, FString(TEXT("tok\\en\nnewline")));
	}

	// A raw control character is escaped rather than emitted, which JSON forbids.
	const FString WithControl = SarkoBackend::MakeAnonymousBody(FString(TEXT("dev")) + FString::Chr(0x01) + TEXT("ice"));
	TSharedPtr<FJsonObject> ControlRoot;
	const TSharedRef<TJsonReader<>> ControlReader = TJsonReaderFactory<>::Create(WithControl);
	TestTrue(TEXT("a control character does not break the body"),
		FJsonSerializer::Deserialize(ControlReader, ControlRoot) && ControlRoot.IsValid());

	// And the ordinary case is untouched — no stray backslashes in a normal body.
	const FString Clean = SarkoBackend::MakeSessionBody(TEXT("sid"), TEXT("stok"));
	TestEqual(TEXT("a clean body is byte-identical to the hand-written shape"), Clean,
		FString(TEXT("{\"session_id\":\"sid\",\"session_token\":\"stok\"}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoRaidActivatesOnceAndNeverAfterItIsSettled,
	"Sarko.Backend.RaidActivatesOnceAndNeverAfterItIsSettled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoRaidActivatesOnceAndNeverAfterItIsSettled::RunTest(const FString& Parameters)
{
	using ESarkoOutcome = ESarkoRaidOutcome;

	// The only state a raid may be activated from.
	TestTrue(TEXT("an unstarted, undecided raid can be activated"),
		SarkoRaid::CanActivateRaid(/*bSessionReady*/ false, ESarkoOutcome::InProgress));

	// Already live: the ordinary double activation, e.g. the offline fallback firing
	// after a confirm has already landed.
	TestFalse(TEXT("a live raid is not activated a second time"),
		SarkoRaid::CanActivateRaid(/*bSessionReady*/ true, ESarkoOutcome::InProgress));

	// Already decided: the case the old guard missed entirely. The damage gate opens
	// on IsRaidFinished() alone, so a player *can* be killed during the
	// auth→start→confirm round trip; the confirm landing afterwards must not hand a
	// corpse a fresh seed (re-rolling every container) and a fresh full clock under
	// its own KIA summary.
	for (const ESarkoOutcome Settled : { ESarkoOutcome::Extracted, ESarkoOutcome::Died, ESarkoOutcome::MIA })
	{
		TestFalse(TEXT("a settled raid is never re-activated, session ready or not"),
			SarkoRaid::CanActivateRaid(/*bSessionReady*/ false, Settled));
		TestFalse(TEXT("nor when the session had already been marked ready"),
			SarkoRaid::CanActivateRaid(/*bSessionReady*/ true, Settled));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSeedFromTheUrlSurvivesTheFullUint32Range,
	"Sarko.Backend.SeedFromTheUrlSurvivesTheFullUint32Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoSeedFromTheUrlSurvivesTheFullUint32Range::RunTest(const FString& Parameters)
{
	// `?Seed=` exists to reproduce a raid whose seed was copied out of a sarko-api
	// log line, and those come from int64(rand.Uint32()) — so roughly half of them
	// exceed INT32_MAX. FCString::Atoi saturates at 2147483647, which quietly rolls
	// a different set of crates than the server logged, so InitGame parses with
	// Atoi64 and wraps through the same SeedToInt32 the online path uses.
	//
	// The parse itself is what is tested here; InitGame needs a world, this does not.
	const TCHAR* Copied = TEXT("3402905197");
	TestEqual(TEXT("a URL seed above INT32_MAX wraps rather than saturating"),
		SarkoBackend::SeedToInt32(FCString::Atoi64(Copied)),
		SarkoBackend::SeedToInt32(3402905197LL));
	// The portable half of the claim: Atoi64 actually holds the value, which an
	// int32-returning Atoi cannot represent at all — its result for this input is
	// platform-dependent (Mac happens to wrap, other platforms clamp), and depending
	// on which is exactly the bug.
	TestEqual(TEXT("Atoi64 holds the whole value instead of narrowing it"),
		FCString::Atoi64(Copied), 3402905197LL);
	TestEqual(TEXT("the URL path and the raid/start path agree bit for bit"),
		SarkoBackend::SeedToInt32(FCString::Atoi64(Copied)),
		static_cast<int32>(static_cast<uint32>(3402905197u)));

	// The ordinary small seed a developer types by hand is untouched.
	TestEqual(TEXT("a small URL seed is unchanged"),
		SarkoBackend::SeedToInt32(FCString::Atoi64(TEXT("12345"))), 12345);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoParsesTheRealProfileResponse,
	"Sarko.Backend.ParsesTheRealProfileResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoParsesTheRealProfileResponse::RunTest(const FString& Parameters)
{
	// Copied from the deployed service, field for field. `stash` is ordered by
	// item_id server-side (store.Profile's ORDER BY), which is why the test can
	// assert positions.
	const FString Body = TEXT(R"({
		"player_id": "a9451008-9665-44d6-aeec-1305d61e53dd",
		"schema_version": 1,
		"stash": [
			{ "item_id": "ammo_9mm", "quantity": 60 },
			{ "item_id": "medkit",   "quantity": 1 },
			{ "item_id": "pistol",   "quantity": 1 }
		],
		"vehicle_tier": "none",
		"unlocked_maps": ["bridge"],
		"tutorial_completed": true
	})");

	FSarkoProfile Profile;
	FString Error;
	TestTrue(TEXT("the live profile shape parses"), SarkoBackend::ParseProfileResponse(Body, Profile, Error));
	TestEqual(TEXT("no error on success"), Error, FString());
	TestEqual(TEXT("player id survives"), Profile.PlayerId, FString(TEXT("a9451008-9665-44d6-aeec-1305d61e53dd")));
	TestEqual(TEXT("schema version survives"), Profile.SchemaVersion, 1);
	TestEqual(TEXT("every stash row is read"), Profile.Stash.Num(), 3);
	TestEqual(TEXT("stash order is the server's"), Profile.Stash[0].Item, FName(TEXT("ammo_9mm")));
	TestEqual(TEXT("stash quantity survives"), Profile.Stash[0].Quantity, 60);
	TestEqual(TEXT("vehicle tier survives"), Profile.VehicleTier, FString(TEXT("none")));
	TestEqual(TEXT("unlocked maps are read"), Profile.UnlockedMaps.Num(), 1);
	TestEqual(TEXT("the only unlocked map is the one this build ships"),
		Profile.UnlockedMaps[0], FString(TEXT("bridge")));
	TestTrue(TEXT("tutorial_completed is read, not defaulted"), Profile.bTutorialCompleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoAbsentTutorialFlagMeansTutorialMode,
	"Sarko.Backend.AbsentTutorialFlagMeansTutorialMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoAbsentTutorialFlagMeansTutorialMode::RunTest(const FString& Parameters)
{
	// Spec §6.5: "no flag → tutorial mode". That has to hold for a *missing*
	// field too, not only for `false`, because a client running against a backend
	// that predates Task 1 receives no field at all. Defaulting the other way
	// would hand a brand-new player the full seeded loot economy on raid one and
	// skip the tutorial permanently, with nothing logging it.
	const FString Body = TEXT(R"({
		"player_id": "11111111-2222-3333-4444-555555555555",
		"schema_version": 1,
		"stash": [],
		"vehicle_tier": "none",
		"unlocked_maps": ["bridge"]
	})");

	FSarkoProfile Profile;
	FString Error;
	TestTrue(TEXT("a profile without the flag still parses"),
		SarkoBackend::ParseProfileResponse(Body, Profile, Error));
	TestFalse(TEXT("an absent flag reads as not completed"), Profile.bTutorialCompleted);
	TestEqual(TEXT("an empty stash is legitimate, not an error"), Profile.Stash.Num(), 0);

	// And the default on a freshly constructed struct must agree, because the
	// offline path never parses anything at all.
	const FSarkoProfile Fresh;
	TestFalse(TEXT("a default-constructed profile is in tutorial mode"), Fresh.bTutorialCompleted);

	// The byte-for-byte body the deployed service answered with on 2026-07-31,
	// before the flag's migration shipped — one line, no whitespace, no flag. The
	// hand-written case above proves the rule; this proves the rule against the
	// actual wire text, including its ordering and its absent field.
	const FString Captured = TEXT(R"({"player_id":"a1b67c01-ec63-40d9-bba9-d76f296e0451","schema_version":1,"stash":[{"item_id":"ammo_9mm","quantity":60},{"item_id":"medkit","quantity":1},{"item_id":"pistol","quantity":1}],"vehicle_tier":"none","unlocked_maps":["bridge"]})");

	FSarkoProfile Live;
	FString LiveError;
	TestTrue(TEXT("the captured production body parses"),
		SarkoBackend::ParseProfileResponse(Captured, Live, LiveError));
	TestEqual(TEXT("captured: player id survives"), Live.PlayerId,
		FString(TEXT("a1b67c01-ec63-40d9-bba9-d76f296e0451")));
	TestEqual(TEXT("captured: schema version survives"), Live.SchemaVersion, 1);
	TestEqual(TEXT("captured: the starter kit is three rows"), Live.Stash.Num(), 3);
	TestEqual(TEXT("captured: first row is the ammo"), Live.Stash[0].Item, FName(TEXT("ammo_9mm")));
	TestEqual(TEXT("captured: ammo count survives"), Live.Stash[0].Quantity, 60);
	TestEqual(TEXT("captured: last row is the pistol"), Live.Stash[2].Item, FName(TEXT("pistol")));
	TestEqual(TEXT("captured: vehicle tier survives"), Live.VehicleTier, FString(TEXT("none")));
	TestEqual(TEXT("captured: one unlocked map"), Live.UnlockedMaps.Num(), 1);
	TestEqual(TEXT("captured: it is bridge"), Live.UnlockedMaps[0], FString(TEXT("bridge")));
	TestFalse(TEXT("captured: a backend older than the flag means tutorial mode"),
		Live.bTutorialCompleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoProfileRejectsBadInput,
	"Sarko.Backend.ProfileRejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoProfileRejectsBadInput::RunTest(const FString& Parameters)
{
	// The shelter draws this and the raid branches on it. A half-parsed profile
	// would show a player an empty stash they actually own, or silently pick the
	// wrong loot mode — both indistinguishable from a backend fault.
	const TArray<TPair<FString, FString>> BadCases = {
		{ TEXT("not json"),            TEXT("{{{") },
		{ TEXT("an error envelope"),   TEXT(R"({"error":{"code":"unauthorized","message":"no player in context"}})") },
		{ TEXT("no player_id"),        TEXT(R"({"schema_version":1,"vehicle_tier":"none"})") },
		{ TEXT("empty player_id"),     TEXT(R"({"player_id":"","schema_version":1,"vehicle_tier":"none"})") },
		{ TEXT("no vehicle_tier"),     TEXT(R"({"player_id":"p","schema_version":1})") },
		{ TEXT("stash not an array"),  TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":7})") },
		{ TEXT("stash row has no id"), TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":[{"quantity":3}]})") },
		{ TEXT("stash row qty zero"),  TEXT(R"({"player_id":"p","vehicle_tier":"none","stash":[{"item_id":"chain","quantity":0}]})") },
	};

	for (const TPair<FString, FString>& Case : BadCases)
	{
		FSarkoProfile Profile;
		FString Error;
		TestFalse(FString::Printf(TEXT("rejected: %s"), *Case.Key),
			SarkoBackend::ParseProfileResponse(Case.Value, Profile, Error));
		TestFalse(FString::Printf(TEXT("names the problem: %s"), *Case.Key), Error.IsEmpty());
		// A failed parse leaves nothing behind: a caller that ignores the return
		// value must not find a plausible-looking half-profile.
		TestTrue(FString::Printf(TEXT("nothing survives a failure: %s"), *Case.Key),
			Profile.PlayerId.IsEmpty() && Profile.Stash.Num() == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoARejectedTokenIsDropped,
	"Sarko.Backend.ARejectedTokenIsDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSarkoARejectedTokenIsDropped::RunTest(const FString& Parameters)
{
	// One client and one JWT now ride the whole app launch (USarkoGameInstance owns
	// them), and both callers skip authentication whenever IsAuthenticated(). So a
	// token kept after a 401 is a token that fails every request until the app is
	// relaunched: a rotated JWT_SECRET or a deleted player row used to heal itself
	// on the next raid, and silently stopped doing so.
	TestTrue(TEXT("a 401 on an authenticated request drops the token"),
		SarkoBackend::ShouldDropTokenOnResponse(/*bAuthenticatedRequest*/ true, 401, TEXT("unauthorized")));

	// A 401 whose body is not this backend's error envelope at all — a proxy's own,
	// say — drops it too. One extra anonymous auth is one cheap round trip; a
	// permanently dead token costs every raid after it.
	TestTrue(TEXT("an unparseable 401 body drops the token as well"),
		SarkoBackend::ShouldDropTokenOnResponse(true, 401, FString()));

	// The one 401 that must NOT: bad_session_token is /v1/raid/result comparing the
	// RAID's session token, which means the Bearer token in front of it was
	// accepted. Dropping it here would spend a re-auth on a fault re-authenticating
	// cannot fix, and would do it on every result submitted with a stale session.
	TestFalse(TEXT("a rejected raid session token leaves the JWT alone"),
		SarkoBackend::ShouldDropTokenOnResponse(true, 401, TEXT("bad_session_token")));

	// Nothing else touches the token. A 403/409/500 says the request was wrong, not
	// the identity behind it, and 409 insufficient_items in particular is a normal
	// answer that must not cost an auth round trip.
	for (const int32 Code : { 200, 400, 403, 404, 409, 422, 429, 500, 503 })
	{
		TestFalse(FString::Printf(TEXT("HTTP %d leaves the token alone"), Code),
			SarkoBackend::ShouldDropTokenOnResponse(true, Code, TEXT("unauthorized")));
	}

	// And an unauthenticated request has no token of its own to invalidate:
	// /v1/auth/anonymous is the call that MAKES one, so reacting to its status here
	// could only ever throw away a token that had just arrived.
	TestFalse(TEXT("an unauthenticated request never drops a token"),
		SarkoBackend::ShouldDropTokenOnResponse(/*bAuthenticatedRequest*/ false, 401, TEXT("unauthorized")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
