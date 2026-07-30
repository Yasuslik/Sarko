#include "Misc/AutomationTest.h"

#include "Core/SarkoRaidSettings.h"
#include "Net/SarkoBackendClient.h"

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
	// deadline: RAID_TTL is 12m and GRACE_BUFFER 2m on the deployed service, and
	// sarko-api/README.md is explicit that the buffer is not play time.
	TestTrue(TEXT("there is a grace margin"), Settings.BackendGraceMarginSeconds >= 60.f);
	TestTrue(TEXT("the HTTP timeout is short enough not to stall a raid"),
		Settings.BackendTimeoutSeconds > 0.f && Settings.BackendTimeoutSeconds <= 20.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
