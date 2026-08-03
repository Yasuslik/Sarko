#include "Shelter/SarkoShelterPlayerController.h"

#include "Core/SarkoGameInstance.h"
#include "Core/SarkoRaidSettings.h"
#include "Core/SarkoTravel.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Loot/SarkoEquipment.h"
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
		.OnEnterSortie(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::EnterSortie))
		.OnCraft(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::Craft))
		.OnSelectScreen(FSarkoOnSelectScreen::CreateUObject(
			this, &ASarkoShelterPlayerController::SelectScreen))
		.OnEquipStack(FSarkoOnEquipStack::CreateUObject(
			this, &ASarkoShelterPlayerController::EquipStack))
		.OnUnequipSlot(FSarkoOnUnequipSlot::CreateUObject(
			this, &ASarkoShelterPlayerController::UnequipSlot));

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
		LastError, LastCraftLine, SarkoLoot::GetItemCatalog(), CurrentScreen, BorrowedKit));
}

void ASarkoShelterPlayerController::SelectScreen(ESarkoShelterScreen Screen)
{
	if (Screen == CurrentScreen)
	{
		return;
	}
	CurrentScreen = Screen;
	// No fetch and no modal: switching is instant (spec §1), and it is a redraw of
	// state this controller already holds.
	RefreshWidget();
}

void ASarkoShelterPlayerController::EquipStack(int32 StackIndex)
{
	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	if (!GameInstance || !Widget.IsValid())
	{
		return;
	}

	// The view's own sorted array, rebuilt the same way the widget drew it — the tap
	// carries an index into what is ON SCREEN, and the profile's stash is in the
	// server's order. Rebuilding it here rather than caching it keeps the index and
	// the cells provably the same list.
	const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();
	const TArray<FSarkoItemStack> Stacks =
		SarkoShelter::BuildStashStacks(GameInstance->CachedProfile, Catalog);
	if (!Stacks.IsValidIndex(StackIndex))
	{
		return;
	}

	const FName Item = Stacks[StackIndex].Item;
	const FSarkoItemDef* Def = Catalog.Find(Item);
	const ESarkoEquipSlot Slot = SarkoEquip::SlotFor(Def);

	// A non-equipment item has no home slot, and its refusal is stated against the
	// slot it would have wanted — which is None, i.e. "this is not equipment". That
	// is the reachable wrong-category case: most of what is in a stash is cargo.
	FString Reason;
	const bool bOccupied = Slot != ESarkoEquipSlot::None
		&& !SarkoEquip::Get(GameInstance->CachedProfile.Equipment, Slot).IsNone();
	if (!SarkoEquip::Accepts(Slot, Def, bOccupied, Reason))
	{
		// The refusal is the RULE's, not this function's: shake the tapped cell, pulse
		// the character plate amber, and say the reason in words.
		Widget->PlayRefusal(StackIndex, Reason);
		return;
	}

	// Applied locally first so the tap is instant, then sent. The server's answer
	// replaces this: it is the authority on what this player owns, and a refusal
	// arrives as a status line and a refetch.
	SarkoEquip::Set(GameInstance->CachedProfile.Equipment, Slot, Item);
	Widget->PlayRefusal(INDEX_NONE, FString());
	RefreshWidget();
	SendEquipment(Slot, Item);
}

void ASarkoShelterPlayerController::UnequipSlot(ESarkoEquipSlot Slot)
{
	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	if (!GameInstance || Slot == ESarkoEquipSlot::None)
	{
		return;
	}
	// Always allowed, for every slot including the weapon. An empty weapon slot is a
	// legal state the raid button names rather than blocks (spec §4).
	SarkoEquip::Set(GameInstance->CachedProfile.Equipment, Slot, NAME_None);
	if (Widget.IsValid())
	{
		Widget->PlayRefusal(INDEX_NONE, FString());
	}
	RefreshWidget();
	SendEquipment(Slot, NAME_None);
}

void ASarkoShelterPlayerController::SendEquipment(ESarkoEquipSlot Slot, FName Item)
{
	if (bDebugSuppressEquipSend)
	{
		// A screenshot run against a FAKED cached stash. See the flag's comment: the
		// rule, the refusal and the view are all real; only the round trip is skipped,
		// because a refusal would refetch and replace the faked stash mid-shot.
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

	// Weak: this completion routinely lands after the player has pressed В РЕЙД and
	// this controller has been destroyed by the travel.
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	Backend->SetEquipment(Slot, Item,
		[WeakThis](bool bSuccess, const FSarkoEquipment& Equipment, const FString& Error)
		{
			ASarkoShelterPlayerController* Self = WeakThis.Get();
			USarkoGameInstance* Instance = Self ? Self->GetGameInstance<USarkoGameInstance>() : nullptr;
			if (!Self || !Instance)
			{
				return;
			}
			if (!bSuccess)
			{
				// not_equippable and insufficient_items arrive here. Shown verbatim,
				// and then the OPTIMISTIC change is thrown away by a refetch: a
				// screen that keeps showing gear the server refused is a screen that
				// will send that gear as a loadout and be refused again.
				Self->LastError = Error;
				Self->FetchProfile();
				Self->RefreshWidget();
				return;
			}
			Self->LastError.Reset();
			// The server's copy, wholesale. It is the answer to "what am I wearing
			// now", and taking it entire is what makes a slot this client got wrong
			// self-correcting.
			Instance->CachedProfile.Equipment = Equipment;
			Self->RefreshWidget();
		});
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

void ASarkoShelterPlayerController::SarkoDebugStash(float DelaySeconds, bool bShortAPart)
{
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis, bShortAPart]()
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
		//
		// It carries one of each EQUIPPABLE item too — a pistol, a bag and a jacket —
		// because "something in every slot" is a state the character panel has to be
		// photographed in, and the jacket is in no loot table yet (spec §5 calls the
		// clothing slot a hook, not a system).
		static const FName Mixed[] = {
			TEXT("scrap_metal"), TEXT("pistol"),      TEXT("bandage"),    TEXT("ammo_9mm"),
			TEXT("medkit"),      TEXT("toolbox"),     TEXT("bike_frame"), TEXT("wheel_small"),
			TEXT("chain"),       TEXT("copper_wire"), TEXT("vodka"),      TEXT("cigarettes"),
			TEXT("duct_tape"),   TEXT("canned_food"), TEXT("painkillers"), TEXT("backpack"),
			TEXT("jacket"),
		};
		static const int32 Amounts[] = { 14, 1, 5, 60, 2, 1, 1, 2, 3, 9, 2, 7, 4, 6, 5, 1, 1 };

		GameInstance->CachedProfile.Stash.Reset();
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Mixed); ++Index)
		{
			GameInstance->CachedProfile.Stash.Add(FSarkoItemStack{ Mixed[Index], Amounts[Index] });
		}
		if (bShortAPart)
		{
			// One wheel where the recipe wants two: the garage block's OTHER state,
			// which a full mixed stash can never show. The 2x2 cell stays in the
			// grid, so the two frames differ in the garage line and nowhere else.
			if (FSarkoItemStack* Wheels = GameInstance->CachedProfile.Stash.FindByPredicate(
				[](const FSarkoItemStack& Stack) { return Stack.Item == FName(TEXT("wheel_small")); }))
			{
				Wheels->Quantity = 1;
			}
		}
		GameInstance->bProfileLoaded = true;
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoDebugStash: the CACHED profile now holds %d mixed stacks%s. Nothing was sent to the backend."),
			GameInstance->CachedProfile.Stash.Num(),
			bShortAPart ? TEXT(", one wheel short of a bicycle") : TEXT(""));
		Self->RefreshWidget();
	}), FMath::Max(0.01f, DelaySeconds), false);
#endif
}

void ASarkoShelterPlayerController::SarkoDebugScreen(int32 ScreenIndex, float DelaySeconds)
{
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis, ScreenIndex]()
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		// Clamped rather than trusted: this is a console argument, and an out-of-range
		// index would put the switcher on a child that does not exist.
		const int32 Clamped = FMath::Clamp(ScreenIndex, 0, 2);
		Self->SelectScreen(static_cast<ESarkoShelterScreen>(Clamped));
		UE_LOG(LogTemp, Warning, TEXT("SarkoDebugScreen: the hub is on screen %d."), Clamped);
	}), FMath::Max(0.01f, DelaySeconds), false);
#endif
}

void ASarkoShelterPlayerController::SarkoDebugEquip(float DelaySeconds, bool bRefuse)
{
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis, bRefuse]()
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		USarkoGameInstance* GameInstance = Self ? Self->GetGameInstance<USarkoGameInstance>() : nullptr;
		if (!GameInstance || !Self->Widget.IsValid())
		{
			return;
		}
		const FSarkoItemCatalog& Catalog = SarkoLoot::GetItemCatalog();

		// The stash as the SCREEN has it, which is what a tap indexes into. Both
		// branches below then go through the real tap handlers, so what is
		// photographed is what a finger would have produced.
		const TArray<FSarkoItemStack> Stacks =
			SarkoShelter::BuildStashStacks(GameInstance->CachedProfile, Catalog);

		// The one thing this run skips. See the flag's comment on the header.
		Self->bDebugSuppressEquipSend = true;

		if (bRefuse)
		{
			// The FIRST cargo item in the grid — something with no equipment slot at
			// all, which is the wrong-category refusal a player reaches by tapping a
			// medkit. The rule and the wording are the shipped ones; only the finger
			// is faked.
			for (int32 Index = 0; Index < Stacks.Num(); ++Index)
			{
				if (SarkoEquip::SlotFor(Catalog.Find(Stacks[Index].Item)) == ESarkoEquipSlot::None)
				{
					Self->EquipStack(Index);
					UE_LOG(LogTemp, Warning,
						TEXT("SarkoDebugEquip: tapped cargo '%s' at cell %d — the refusal on screen is the real rule's."),
						*Stacks[Index].Item.ToString(), Index);
					return;
				}
			}
			UE_LOG(LogTemp, Error,
				TEXT("SarkoDebugEquip: no cargo item in the stash to be refused — run SarkoDebugStash first."));
			return;
		}

		// One tap per slot, first match wins, and every one of them goes through
		// EquipStack — so nothing here can equip something SarkoEquip::Accepts would
		// have refused.
		int32 Equipped = 0;
		for (ESarkoEquipSlot Slot : SarkoEquip::Slots())
		{
			for (int32 Index = 0; Index < Stacks.Num(); ++Index)
			{
				if (SarkoEquip::SlotFor(Catalog.Find(Stacks[Index].Item)) == Slot)
				{
					Self->EquipStack(Index);
					++Equipped;
					break;
				}
			}
		}
		UE_LOG(LogTemp, Warning,
			TEXT("SarkoDebugEquip: filled %d of %d slot(s) from the CACHED stash. Nothing was entitled; /v1/raid/start still debits the real one."),
			Equipped, SarkoEquip::Slots().Num());
	}), FMath::Max(0.01f, DelaySeconds), false);
#endif
}

void ASarkoShelterPlayerController::SarkoDebugSortie(float DelaySeconds, int32 CooldownSeconds)
{
#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakThis, CooldownSeconds]()
	{
		ASarkoShelterPlayerController* Self = WeakThis.Get();
		USarkoGameInstance* GameInstance = Self ? Self->GetGameInstance<USarkoGameInstance>() : nullptr;
		if (!GameInstance)
		{
			return;
		}
		GameInstance->bProfileLoaded = true;

		if (CooldownSeconds > 0)
		{
			// The cooldown state. It goes into the CACHED PROFILE and not into the
			// button, so what is photographed is BuildRaidButton reading the same field
			// /v1/profile fills — the label, the colour and the refusal are all the
			// shipped ones.
			GameInstance->CachedProfile.SortieCooldownSeconds = CooldownSeconds;
			Self->BorrowedKit.Reset();
			UE_LOG(LogTemp, Warning,
				TEXT("SarkoDebugSortie: the CACHED profile now claims %d s of cooldown. Nothing was sent to the backend, and the server would refuse or allow a sortie on its own count."),
				CooldownSeconds);
		}
		else
		{
			// The reveal. A copy of the server's richest authored kit ("provisioned" in
			// domain.SortieKits) — the only copy of that table on this client, and it
			// exists to be drawn and nothing else. It is NOT written into the profile's
			// equipment: the player owns none of it until an extraction credits it.
			GameInstance->CachedProfile.SortieCooldownSeconds = 0;
			Self->BorrowedKit = {
				FSarkoItemStack{ FName(TEXT("pistol")), 1 },
				FSarkoItemStack{ FName(TEXT("ammo_9mm")), 20 },
				FSarkoItemStack{ FName(TEXT("backpack")), 1 },
				FSarkoItemStack{ FName(TEXT("jacket")), 1 },
				FSarkoItemStack{ FName(TEXT("medkit")), 1 },
			};
			UE_LOG(LogTemp, Warning,
				TEXT("SarkoDebugSortie: showing a FAKE borrowed kit of %d stack(s) in the character panel. No session was opened, nothing was granted, and no travel is scheduled."),
				Self->BorrowedKit.Num());
		}
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

void ASarkoShelterPlayerController::EnterSortie()
{
	if (bSortieInFlight)
	{
		// A second press would hold a second open session against the first, and the
		// server would refuse it `raid_in_progress` — the right refusal for the wrong
		// reason, on a button that had just worked.
		return;
	}

	USarkoGameInstance* GameInstance = GetGameInstance<USarkoGameInstance>();
	const TSharedPtr<FSarkoBackendClient> Backend = GameInstance ? GameInstance->EnsureBackend() : nullptr;
	if (!Backend.IsValid())
	{
		// A sortie is worthless offline: the kit is the server's, so there is nothing
		// to borrow and nothing to credit. Unlike В РЕЙД — which must work offline,
		// because a player with nothing must always have a way in — this one says so
		// and does not travel.
		LastError = TEXT("no backend client");
		RefreshWidget();
		return;
	}

	bSortieInFlight = true;
	// The map is the same one an ordinary raid enters, and the same setting decides it:
	// a sortie is a raid with a worse kit and a shorter clock, not a different place.
	const FString MapId = GetDefault<USarkoRaidSettings>()->BackendMapId;

	// Weak: this completion can land after the reveal timer has already travelled, or
	// after the player has pressed В РЕЙД instead.
	TWeakObjectPtr<ASarkoShelterPlayerController> WeakThis(this);
	Backend->StartRaid(MapId, /*Loadout*/ {},
		[WeakThis](bool bStarted, const FSarkoRaidSession& Session, const FString& Error)
		{
			ASarkoShelterPlayerController* Self = WeakThis.Get();
			USarkoGameInstance* Instance = Self ? Self->GetGameInstance<USarkoGameInstance>() : nullptr;
			if (!Self || !Instance)
			{
				return;
			}
			Self->bSortieInFlight = false;

			if (!bStarted)
			{
				// `sortie_cooldown` arrives here, and its message names the remaining
				// time. Shown VERBATIM, like every other refusal on this screen — and
				// then the profile is refetched, because the server's countdown is the
				// only honest source for the button's label and the one this client had
				// was evidently wrong.
				Self->LastError = Error;
				Self->FetchProfile();
				Self->RefreshWidget();
				return;
			}
			Self->LastError.Reset();

			if (!Session.IsSortie())
			{
				// The service answered `raid` to a request that said `sortie` — an older
				// deployment, or one that dropped the field. It is a REAL raid now: it
				// debited nothing (the loadout was empty), but nothing about it is free
				// or kitted, so saying "ПОЗИЧЕНЕ" over an empty character panel would be
				// the screen inventing a mechanic the server does not have.
				UE_LOG(LogTemp, Warning,
					TEXT("SarkoShelter: asked for a sortie and the server opened a '%s' session — entering it as an ordinary raid"),
					*Session.Mode);
			}

			// Parked on the game instance, which is the only thing that survives the
			// travel this is about to start. ASarkoRaidGameMode adopts it instead of
			// opening a second session.
			Instance->PendingSortie = Session;
			Self->BorrowedKit = Session.GrantedKit;
			UE_LOG(LogTemp, Display,
				TEXT("SarkoShelter: ВИЛАЗКА %s granted %d stack(s) — showing them, then travelling"),
				*Session.SessionId, Session.GrantedKit.Num());

			// The reveal: the borrowed kit goes into the character panel, and the travel
			// waits long enough for it to be read.
			Self->RefreshWidget();
			Self->TravelAfterSortieReveal();
		},
		TEXT("sortie"));

	// Drawn immediately so the press is acknowledged: the button greys out on the next
	// SetView anyway, and a button that looks untouched for a whole round trip reads as
	// a button that did not work.
	RefreshWidget();
}

void ASarkoShelterPlayerController::TravelAfterSortieReveal()
{
	// CreateWeakLambda, so a timer still pending when this controller is destroyed —
	// the player pressed В РЕЙД during the reveal — fires nothing. A second travel out
	// of a half-unloaded shelter is not a recoverable state.
	GetWorldTimerManager().SetTimer(SortieRevealTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { EnterRaid(); }),
		SortieRevealSeconds, /*bLoop*/ false);
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
