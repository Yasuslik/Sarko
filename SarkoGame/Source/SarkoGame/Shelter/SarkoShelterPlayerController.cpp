#include "Shelter/SarkoShelterPlayerController.h"

#include "Core/SarkoGameInstance.h"
#include "Core/SarkoTravel.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Loot/SarkoItemCatalog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/SarkoBackendClient.h"
#include "Shelter/SarkoShelterView.h"
#include "Shelter/SarkoShelterWidget.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * One automated raid per process, so the shelter the raid *returns* to is left
	 * alone. BeginPlay runs on every shelter entry, including that one, and firing
	 * again there would bounce straight back into a second raid — hiding the return
	 * screen, which is the one that carries the outcome and the credited haul.
	 *
	 * Prefixed and long-named on purpose: a unity build puts this file in the same
	 * translation unit as whatever else the blob happens to contain, and a short
	 * file-scope name here has already collided with a local in another file.
	 */
	bool GSarkoAutoRaidFired = false;

	/** 60 half-second polls = 30 s, which is far longer than auth + /v1/profile
	 *  and short enough that a broken run fails loudly instead of hanging. */
	constexpr int32 GSarkoAutoRaidMaxAttempts = 60;
	constexpr float GSarkoAutoRaidPollSeconds = 0.5f;
}
#endif

ASarkoShelterPlayerController::ASarkoShelterPlayerController()
{
	// A menu wants a cursor on desktop; on a phone Slate routes touches as
	// pointer events and this changes nothing.
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ASarkoShelterPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Local only: a widget belongs to a viewport, and a remote controller has
	// none. Also the guard that keeps this doing nothing in a headless automation
	// run, where GetGameViewport() is null.
	if (!IsLocalController())
	{
		return;
	}
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		UE_LOG(LogTemp, Warning, TEXT("SarkoShelter: no game viewport, so no menu. Headless run?"));
		return;
	}

	Widget = SNew(SSarkoShelterWidget)
		.OnEnterRaid(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::EnterRaid))
		.OnCraft(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::Craft));

	Viewport->AddViewportWidgetContent(Widget.ToSharedRef());

	// UI only: there is no pawn and nothing to steer, and leaving game input live
	// would let the desktop test keys (WASD/E from ASarkoPlayerController's
	// #if !UE_BUILD_SHIPPING path) reach a world that has no pawn in it.
	//
	// The focus target is the raid button and not the whole widget:
	// SCompoundWidget is not focusable, and asking for it logs
	// "Attempting to focus Non-Focusable widget" at Error every boot.
	SetInputMode(FInputModeUIOnly().SetWidgetToFocus(Widget->WidgetToFocus()));

	// Draw immediately with whatever the game instance already knows — the raid's
	// outcome, and the previous profile if there is one — then refresh behind it.
	RefreshWidget();
	FetchProfile();

#if !UE_BUILD_SHIPPING
	// `-SarkoShelterShot=<seconds>` photographs the shelter on *every* entry,
	// including the one the raid travels back to. -ExecCmds cannot do that:
	// UnrealEngine.cpp queues those commands exactly once at engine init
	// (ParseExecCommands::QueueDeferredCommands), so a SarkoShelterShot issued on
	// the command line runs in whichever world booted and never again — and the
	// shelter-after-a-raid is precisely the screen that has to be looked at, since
	// it is the only one that has an outcome banner and a haul on it.
	float AutoShotDelay = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SarkoShelterShot="), AutoShotDelay))
	{
		SarkoShelterShot(AutoShotDelay);
	}

	// `-SarkoAutoRaid=<seconds>` presses the raid button, for a run with no fingers.
	// Read on every shelter entry for the same reason as the shot switch above, and
	// then rate-limited to once per process by GSarkoAutoRaidFired.
	float AutoRaidDelay = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SarkoAutoRaid="), AutoRaidDelay))
	{
		StartAutoRaid(AutoRaidDelay);
	}
#endif
}

void ASarkoShelterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Before Super, and unconditionally. A viewport widget is not an actor and is
	// not destroyed with the level: leaving it added means the shelter menu is
	// still on screen during the raid, on top of the HUD, with the raid playing
	// underneath it.
	if (Widget.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
		}
		Widget.Reset();
	}

	// And hand the input mode back, for the same reason the widget is removed
	// here: the UI-only mode set in BeginPlay is *not* world state.
	// FInputModeUIOnly::ApplyInputMode calls UGameViewportClient::SetIgnoreInput
	// (true), the viewport client belongs to the ULocalPlayer, and UEngine::LoadMap
	// destroys this controller while keeping that local player — so bIgnoreInput
	// rode into the raid, where UGameViewportClient::InputKey/InputAxis/InputTouch
	// each early-return on it. The raid then spawned, ran its clock and ignored
	// every stick, shot and loot press until it ended MIA.
	//
	// Belt and braces with ASarkoPlayerController::BeginPlay, which asserts
	// game-only input on its own: this end covers travel to anything that is not
	// the raid, and that end covers a raid entered from anywhere that is not the
	// shelter.
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
	}
	Super::EndPlay(EndPlayReason);
}

void ASarkoShelterPlayerController::RefreshWidget()
{
	const USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	if (!Widget.IsValid() || !GameInstance)
	{
		return;
	}
	Widget->SetView(SarkoShelter::BuildView(
		GameInstance->LastRaid, GameInstance->CachedProfile, GameInstance->bProfileLoaded,
		LastError, LastCraftLine, SarkoLoot::GetItemCatalog()));
}

void ASarkoShelterPlayerController::SarkoDebugParts(float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis]()
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		USarkoGameInstance* GameInstance = Self ? Self->GetGameInstance<USarkoGameInstance>() : nullptr;
		if (!GameInstance)
		{
			return;
		}
		// The mirrored recipe, not a hand-written list: if garage.go's recipe
		// moves, this moves with it and the shot keeps showing the truth.
		for (const FSarkoItemStack& Part : SarkoShelter::BicycleRecipe())
		{
			if (FSarkoItemStack* Held = GameInstance->CachedProfile.Stash.FindByPredicate(
				[&Part](const FSarkoItemStack& Stack) { return Stack.Item == Part.Item; }))
			{
				Held->Quantity = FMath::Max(Held->Quantity, Part.Quantity);
			}
			else
			{
				GameInstance->CachedProfile.Stash.Add(Part);
			}
		}
		GameInstance->bProfileLoaded = true;
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoDebugParts: the CACHED profile now holds a bicycle's parts. Nothing was sent to the backend."));
		Self->RefreshWidget();
	}), FMath::Max(0.01f, DelaySeconds), false);
#endif
}

void ASarkoShelterPlayerController::SarkoDebugStash(float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis]()
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		USarkoGameInstance* GameInstance = Self ? Self->GetGameInstance<USarkoGameInstance>() : nullptr;
		if (!GameInstance)
		{
			return;
		}
		// Deliberately UNSORTED and spanning every category, with a 2x1, a 2x2 and
		// a 3x2 in it: the sort is what the frame is checking, and a multi-cell item
		// is what proves the cell draws one box rather than w boxes with seams.
		static const FName Mixed[] = {
			TEXT("scrap_metal"), TEXT("pistol"),      TEXT("bandage"),    TEXT("ammo_9mm"),
			TEXT("medkit"),      TEXT("toolbox"),     TEXT("bike_frame"), TEXT("wheel_small"),
			TEXT("chain"),       TEXT("copper_wire"), TEXT("vodka"),      TEXT("cigarettes"),
			TEXT("duct_tape"),   TEXT("canned_food"), TEXT("painkillers"), TEXT("backpack"),
		};
		static const int32 Amounts[] = { 14, 1, 5, 60, 2, 1, 1, 2, 3, 9, 2, 7, 4, 6, 5, 1 };

		GameInstance->CachedProfile.Stash.Reset();
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Mixed); ++Index)
		{
			GameInstance->CachedProfile.Stash.Add(FSarkoItemStack{ Mixed[Index], Amounts[Index] });
		}
		GameInstance->bProfileLoaded = true;
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoDebugStash: the CACHED profile now holds %d mixed stacks. Nothing was sent to the backend."),
			GameInstance->CachedProfile.Stash.Num());
		Self->RefreshWidget();
	}), FMath::Max(0.01f, DelaySeconds), false);
#endif
}

void ASarkoShelterPlayerController::Craft()
{
	if (bCraftInFlight)
	{
		return;
	}
	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	const TSharedPtr<FSarkoBackendClient> Backend = GameInstance ? GameInstance->EnsureBackend() : nullptr;
	if (!Backend.IsValid())
	{
		LastError = TEXT("no backend client");
		RefreshWidget();
		return;
	}

	// Snapshotted BEFORE the call, because the answer is a set difference and the
	// profile is about to be replaced by the refetch below.
	const TArray<FString> Before = GameInstance->CachedProfile.UnlockedMaps;

	bCraftInFlight = true;
	if (Widget.IsValid())
	{
		Widget->SetCraftInFlight(true);
	}

	// Weak: this completion routinely lands after the player has pressed В РЕЙД
	// and this controller has been destroyed by the travel.
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	Backend->CraftVehicle([WeakThis, Before](bool bSuccess, const FString& Tier,
		const TArray<FString>& Maps, const FString& Error)
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bCraftInFlight = false;
		if (!bSuccess)
		{
			// insufficient_items and max_tier arrive here. Shown verbatim: a
			// refused craft with no reason is worse than no button.
			Self->LastError = Error;
			Self->RefreshWidget();
			return;
		}
		Self->LastError.Reset();

		const TArray<FString> Opened = SarkoShelter::NewlyUnlockedMaps(Before, Maps);
		Self->LastCraftLine = Opened.Num() > 0
			? FString::Printf(TEXT("ЗІБРАНО. ВІДКРИТО: %s"), *FString::Join(Opened, TEXT(", ")).ToUpper())
			: FString(TEXT("ЗІБРАНО."));

		// The parts have left the stash and the tier has moved, both server-side
		// in one transaction. Refetch rather than patch the cached profile: the
		// server's copy is the only one that knows what the debit actually took.
		Self->FetchProfile();
		Self->RefreshWidget();
	});

	RefreshWidget();
}

void ASarkoShelterPlayerController::FetchProfile()
{
	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	if (!GameInstance)
	{
		LastError = TEXT("no USarkoGameInstance — check GameInstanceClass in DefaultEngine.ini");
		RefreshWidget();
		return;
	}

	const TSharedPtr<FSarkoBackendClient> Backend = GameInstance->EnsureBackend();
	if (!Backend.IsValid())
	{
		LastError = TEXT("no backend client");
		RefreshWidget();
		return;
	}

	// Weak through both hops: these completions routinely land after the player
	// has pressed В РЕЙД and this controller has been destroyed by the travel.
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);

	const auto Fetch = [WeakThis, Backend]()
	{
		Backend->FetchProfile([WeakThis](bool bSuccess, const FSarkoProfile& Profile, const FString& Error)
		{
			ASarkoShelterPlayerController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			USarkoGameInstance* Instance = Self->GetGameInstance<USarkoGameInstance>();
			if (bSuccess && Instance)
			{
				Instance->RecordProfile(Profile);
				Self->LastError.Reset();
			}
			else
			{
				Self->LastError = Error;
			}
			Self->RefreshWidget();
		});
	};

	if (Backend->IsAuthenticated())
	{
		Fetch();
		return;
	}

	Backend->Authenticate([WeakThis, Fetch](bool bAuthenticated, const FString& Error)
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (!bAuthenticated)
		{
			Self->LastError = Error;
			Self->RefreshWidget();
			return;
		}
		Fetch();
	});
}

void ASarkoShelterPlayerController::EnterRaid()
{
	UE_LOG(LogTemp, Display, TEXT("SarkoShelter: entering the raid"));
	// Seed 0 means "no override", so the raid uses the backend's seed (or its own
	// default offline). ?Seed= stays a command-line tool for reproducing a raid.
	SarkoTravel::TravelTo(this, SarkoTravel::RaidOptions(/*SeedOverride*/ 0));
}

#if !UE_BUILD_SHIPPING
void ASarkoShelterPlayerController::StartAutoRaid(float DelaySeconds)
{
	if (GSarkoAutoRaidFired)
	{
		UE_LOG(LogTemp, Display,
			TEXT("SarkoShelter: -SarkoAutoRaid already fired once in this process; this shelter entry is left alone."));
		return;
	}
	AutoRaidAttempts = 0;
	GetWorldTimerManager().SetTimer(AutoRaidTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { TryAutoRaid(); }),
		GSarkoAutoRaidPollSeconds, /*bLoop*/ true, FMath::Max(DelaySeconds, 0.05f));
}

void ASarkoShelterPlayerController::TryAutoRaid()
{
	++AutoRaidAttempts;
	if (!Widget.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AutoRaidTimer);
		return;
	}
	if (Widget->SimulateEnterRaidClickIfEnabled())
	{
		GSarkoAutoRaidFired = true;
		GetWorldTimerManager().ClearTimer(AutoRaidTimer);
		UE_LOG(LogTemp, Display,
			TEXT("SarkoShelter: -SarkoAutoRaid pressed 'В РЕЙД' on poll %d (the button was enabled)."),
			AutoRaidAttempts);
		return;
	}
	if (AutoRaidAttempts >= GSarkoAutoRaidMaxAttempts)
	{
		GetWorldTimerManager().ClearTimer(AutoRaidTimer);
		UE_LOG(LogTemp, Error,
			TEXT("SarkoShelter: -SarkoAutoRaid gave up after %d polls — 'В РЕЙД' never became enabled, so the profile never landed."),
			AutoRaidAttempts);
	}
}
#endif

void ASarkoShelterPlayerController::SarkoShelterShot(float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ConsoleCommand(TEXT("Shot showui"), /*bWriteToLog*/ true);
	}), FMath::Max(DelaySeconds, 0.05f), false);
#endif
}
