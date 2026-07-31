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
		.OnEnterRaid(FSimpleDelegate::CreateUObject(this, &ASarkoShelterPlayerController::EnterRaid));

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
		LastError, SarkoLoot::GetItemCatalog()));
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
