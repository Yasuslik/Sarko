#include "UI/SarkoHUD.h"

#include "Combat/SarkoWeapon.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Engine/Canvas.h"
#include "Loot/SarkoBackpack.h"
#include "Loot/SarkoExtractionZone.h"
#include "Loot/SarkoLootContainer.h"
#include "Map/SarkoMapDefinition.h"
#include "Pawn/SarkoCharacter.h"

void ASarkoHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	DrawStick(PC->GetMoveStick(), FLinearColor(1.f, 1.f, 1.f, 0.35f));
	DrawStick(PC->GetAimStick(), FLinearColor(1.f, 0.85f, 0.2f, 0.45f));
	DrawAimCone();
	DrawTopBar();
	DrawHealth();
	DrawAmmo();
	DrawBackpack();
	DrawInteract();
	DrawExtraction();
	// Last, so the final screen is over everything else rather than under it.
	DrawOutcomeSummary();
}

void ASarkoHUD::DrawStick(const FSarkoTouchStick& Stick, const FLinearColor& Colour)
{
	if (!Stick.bActive)
	{
		return;
	}

	// Ring at the thumb's landing point, dot at the current position.
	const int32 Segments = 24;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float A0 = (2.f * PI * i) / Segments;
		const float A1 = (2.f * PI * (i + 1)) / Segments;
		DrawLine(
			Stick.Origin.X + FMath::Cos(A0) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.Y + FMath::Sin(A0) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.X + FMath::Cos(A1) * FSarkoTouchStick::RadiusPx,
			Stick.Origin.Y + FMath::Sin(A1) * FSarkoTouchStick::RadiusPx,
			Colour, 2.f);
	}
	DrawRect(Colour, Stick.Current.X - 12.f, Stick.Current.Y - 12.f, 24.f, 24.f);
}

void ASarkoHUD::DrawAimCone()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->IsAiming())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const FVector Muzzle = Pawn->GetMuzzleLocation();
	const FVector Aim = FVector(Pawn->AimDirection);

	// Two edges of the cone, projected to screen. On a touchscreen there is no
	// cursor, so without this the player is shooting blind.
	const auto ProjectAndDraw = [this, &Muzzle](const FVector& End, const FLinearColor& Colour)
	{
		const FVector Start2D = Project(Muzzle);
		const FVector End2D = Project(End);
		DrawLine(Start2D.X, Start2D.Y, End2D.X, End2D.Y, Colour, 1.5f);
	};

	const FVector Left = Muzzle + Aim.RotateAngleAxis(Settings.AimConeHalfAngleDegrees, FVector::UpVector) * Settings.WeaponRangeUU;
	const FVector Right = Muzzle + Aim.RotateAngleAxis(-Settings.AimConeHalfAngleDegrees, FVector::UpVector) * Settings.WeaponRangeUU;

	const FLinearColor Colour(1.f, 0.85f, 0.2f, 0.5f);
	ProjectAndDraw(Left, Colour);
	ProjectAndDraw(Right, Colour);
}

void ASarkoHUD::DrawTopBar()
{
	// Top only: the lower corners are dead zones under the player's thumbs.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState)
	{
		return;
	}

	// Rebuilt on the second, not on the frame: the string only ever changes when
	// the integer second does, and DrawHUD is a tick path where a Printf and a
	// GetTextSize per frame is 59 of every 60 rebuilds discarded.
	const int32 Total = FMath::CeilToInt(RaidState->RemainingSeconds);
	if (Total != CachedClockSeconds)
	{
		CachedClockSeconds = Total;
		CachedClock = FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
		float ClockHeight = 0.f;
		GetTextSize(CachedClock, CachedClockWidth, ClockHeight, GEngine->GetLargeFont(), 1.f);
	}

	DrawText(CachedClock, FLinearColor::White, (Canvas->SizeX - CachedClockWidth) * 0.5f, 24.f, GEngine->GetLargeFont(), 1.f);

	// The player must be able to tell "the raid has not started yet" from "the
	// crates are broken". Spec §4.6's loud degradation is a log line for the
	// developer; this is the same fact for the player. Rebuilt per frame rather
	// than cached, unlike the clock: it is on screen for one HTTP round trip, so
	// there is no steady state worth optimising.
	if (!RaidState->bSessionReady)
	{
		const FString Connecting = TEXT("З'ЄДНАННЯ...");
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Connecting, Width, Height, GEngine->GetLargeFont(), 1.f);
		DrawText(Connecting, FLinearColor(1.f, 0.75f, 0.2f), (Canvas->SizeX - Width) * 0.5f, 56.f,
			GEngine->GetLargeFont(), 1.f);
	}
}

void ASarkoHUD::DrawHealth()
{
	// Health was missing entirely, and its absence produced the worst kind of
	// bug report: "the character walks, then stops walking when enemies show
	// up". The character had died — death disables movement — and nothing on
	// screen said so. A player must always be able to see that they are dying,
	// and that they are dead.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->HealthComponent)
	{
		return;
	}

	const USarkoHealthComponent* Health = Pawn->HealthComponent;
	const float Fraction = Health->GetMaxHealth() > 0.f
		? FMath::Clamp(Health->GetHealth() / Health->GetMaxHealth(), 0.f, 1.f)
		: 0.f;

	constexpr float BarWidth = 260.f;
	constexpr float BarHeight = 14.f;
	const float BarX = Canvas->SizeX - BarWidth - 24.f;
	constexpr float BarY = 28.f;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), BarX - 2.f, BarY - 2.f, BarWidth + 4.f, BarHeight + 4.f);

	// Green through red, so falling health is readable without reading a number.
	const FLinearColor Fill = FMath::Lerp(FLinearColor(0.85f, 0.15f, 0.1f), FLinearColor(0.3f, 0.85f, 0.25f), Fraction);
	DrawRect(Fill, BarX, BarY, BarWidth * Fraction, BarHeight);

	if (!Health->IsDead())
	{
		return;
	}

	// Once the raid has an outcome, DrawOutcomeSummary says KIA in the same place
	// and says it better. Two centred death banners stacked on each other is
	// worse than either alone.
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (RaidState && RaidState->IsRaidFinished())
	{
		return;
	}

	// Unmissable, centred: this is the state the tester could not previously see.
	const FString DeadText = TEXT("YOU DIED");
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(DeadText, TextWidth, TextHeight, GEngine->GetLargeFont(), 2.f);
	DrawText(DeadText, FLinearColor(1.f, 0.2f, 0.15f),
		(Canvas->SizeX - TextWidth) * 0.5f, Canvas->SizeY * 0.42f, GEngine->GetLargeFont(), 2.f);
}

void ASarkoHUD::DrawAmmo()
{
	// Top-left, alongside the clock: still along the top per spec §9, clear of
	// both bottom corners so the sticks never cover it. Reloading is drawn as
	// distinct text rather than a number so the tester can tell "reloading"
	// from "empty and stuck" at a glance — the whole point of this readout.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->WeaponComponent)
	{
		return;
	}

	const USarkoWeaponComponent* Weapon = Pawn->WeaponComponent;
	const bool bReloading = Weapon->IsReloading();

	// Rebuilt when the readout changes, which is on a shot or a reload rather than
	// on a frame. FromInt allocates, and so does turning the TEXT("RELOADING")
	// literal into the FString DrawText takes — both were paid every frame for a
	// string with two digits' worth of variation.
	const int32 AmmoKey = bReloading ? INDEX_NONE : Weapon->GetAmmoInMagazine();
	if (AmmoKey != CachedAmmoKey)
	{
		CachedAmmoKey = AmmoKey;
		CachedAmmoText = bReloading ? FString(TEXT("RELOADING")) : FString::FromInt(AmmoKey);
	}

	const FLinearColor Colour = bReloading ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;

	DrawText(CachedAmmoText, Colour, 24.f, 24.f, GEngine->GetLargeFont(), 1.f);
}

void ASarkoHUD::DrawBackpack()
{
	// Top-left, immediately right of the ammo readout: spec §9 puts every
	// number along the top, and the bottom corners belong to the thumbs.
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || !Pawn->BackpackComponent)
	{
		return;
	}

	const USarkoBackpackComponent* Backpack = Pawn->BackpackComponent;
	const int32 Used = Backpack->GetUsedSlots();
	const int32 Limit = Backpack->GetSlotLimit();

	// Rebuilt when a slot is taken or the limit changes — a few times a raid —
	// rather than every frame. Both halves are the key because the limit comes from
	// a setting, not a constant.
	if (Used != CachedBackpackUsed || Limit != CachedBackpackLimit)
	{
		CachedBackpackUsed = Used;
		CachedBackpackLimit = Limit;
		CachedBackpackText = FString::Printf(TEXT("%d/%d"), Used, Limit);
	}

	// The x offset is measured from the widest string DrawAmmo can produce
	// rather than guessed: "RELOADING" in the large font is far wider than the
	// two digits of a magazine count, and a fixed offset that clears "30"
	// overlaps it the moment the player reloads.
	//
	// Measured once and kept. DrawHUD is a tick path and GetTextSize takes an
	// FString, so doing this inline allocated and freed a string every frame to
	// re-derive a constant.
	if (CachedReloadingWidth < 0.f)
	{
		float AmmoHeight = 0.f;
		GetTextSize(TEXT("RELOADING"), CachedReloadingWidth, AmmoHeight, GEngine->GetLargeFont(), 1.f);
	}
	const float X = 24.f + CachedReloadingWidth + 24.f;

	// Amber when full, so "the crate had more in it" is legible at a glance
	// rather than being discovered by counting.
	const FLinearColor Colour = Used >= Limit ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;
	DrawText(CachedBackpackText, Colour, X, 24.f, GEngine->GetLargeFont(), 1.f);
}

void ASarkoHUD::DrawInteract()
{
	const ASarkoPlayerController* PC = Cast<ASarkoPlayerController>(PlayerOwner);
	if (!PC)
	{
		return;
	}

	// The button is always drawn, so the player learns where it is before they
	// need it; it dims when there is nothing in reach.
	const FBox2D Rect = SarkoInput::InteractButtonRect(FVector2D(Canvas->SizeX, Canvas->SizeY));
	const ASarkoLootContainer* Target = PC->GetInteractTarget();
	const FLinearColor ButtonColour = Target
		? FLinearColor(0.95f, 0.8f, 0.25f, 0.55f)
		: FLinearColor(1.f, 1.f, 1.f, 0.15f);
	DrawRect(ButtonColour, Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y);

	// One character, but an FString construction and a GetTextSize all the same,
	// and both were paid every frame for a label that cannot change. Hoisted like
	// CachedReloadingWidth; the string itself is a static so DrawText below is not
	// rebuilding it either.
	static const FString InteractLabel(TEXT("E"));
	if (CachedInteractLabelWidth < 0.f)
	{
		GetTextSize(InteractLabel, CachedInteractLabelWidth, CachedInteractLabelHeight, GEngine->GetLargeFont(), 1.f);
	}
	DrawText(InteractLabel, FLinearColor::White,
		Rect.GetCenter().X - CachedInteractLabelWidth * 0.5f, Rect.GetCenter().Y - CachedInteractLabelHeight * 0.5f,
		GEngine->GetLargeFont(), 1.f);

	if (!Target)
	{
		return;
	}

	// Prompt: top-centre, under the clock. Never a bottom corner (spec §9).
	//
	// Built and measured only when the tier changes, which is the only thing the
	// text depends on. Doing it inline cost a Printf, an FName::ToString and a
	// GetTextSize every frame the player stood near a crate.
	if (!bPromptCached || Target->Tier != CachedPromptTier)
	{
		bPromptCached = true;
		CachedPromptTier = Target->Tier;
		CachedPrompt = FString::Printf(TEXT("ОБШУКАТИ (%s)"), *CachedPromptTier.ToString());
		GetTextSize(CachedPrompt, CachedPromptWidth, CachedPromptHeight, GEngine->GetLargeFont(), 1.f);
	}

	const float PromptWidth = CachedPromptWidth;
	const float PromptHeight = CachedPromptHeight;
	const float PromptX = (Canvas->SizeX - PromptWidth) * 0.5f;
	constexpr float PromptY = 76.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), PromptX - 10.f, PromptY - 4.f, PromptWidth + 20.f, PromptHeight + 8.f);
	DrawText(CachedPrompt, FLinearColor::White, PromptX, PromptY, GEngine->GetLargeFont(), 1.f);

	if (!PC->IsInteractHeld())
	{
		return;
	}

	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	// Progress: local and cosmetic. The server owns whether the channel
	// completes; this bar only has to stop the player wondering whether the
	// hold is doing anything.
	const float Duration = FMath::Max(0.01f, GetDefault<USarkoRaidSettings>()->LootChannelSeconds);
	const float Fraction = FMath::Clamp(Pawn->GetLootChannelElapsed() / Duration, 0.f, 1.f);

	constexpr float BarWidth = 260.f;
	constexpr float BarHeight = 12.f;
	const float BarX = (Canvas->SizeX - BarWidth) * 0.5f;
	const float BarY = PromptY + PromptHeight + 10.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), BarX - 2.f, BarY - 2.f, BarWidth + 4.f, BarHeight + 4.f);
	DrawRect(FLinearColor(0.95f, 0.8f, 0.25f, 0.9f), BarX, BarY, BarWidth * Fraction, BarHeight);
}

const FString& ASarkoHUD::ZoneNameFor(int32 ZoneIndex)
{
	// Fallback, not a label: a generic word here means the map file and the
	// server's zone list have drifted, which should be visible rather than
	// crash the HUD.
	static const FString Generic(TEXT("ЕВАКУАЦІЯ"));

	if (!bZoneNamesCached)
	{
		// Once per HUD, not once per frame. DrawHUD is a tick path.
		bZoneNamesCached = true;
		FSarkoMapDefinition Definition;
		FString Error;
		if (SarkoMap::LoadDefinitionFromDisk(GetDefault<USarkoRaidSettings>()->MapId.ToString(), Definition, Error))
		{
			CachedZoneNames.Reserve(Definition.Extractions.Num());
			for (const FSarkoExtractionSpot& Spot : Definition.Extractions)
			{
				CachedZoneNames.Add(Spot.Name.IsEmpty() ? Generic : Spot.Name);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SarkoHUD: extraction zone names unavailable: %s"), *Error);
		}
	}

	return CachedZoneNames.IsValidIndex(ZoneIndex) ? CachedZoneNames[ZoneIndex] : Generic;
}

void ASarkoHUD::DrawExtraction()
{
	const ASarkoCharacter* Pawn = Cast<ASarkoCharacter>(GetOwningPawn());
	if (!Pawn || Pawn->ExtractZoneIndex == INDEX_NONE)
	{
		return;
	}

	const float Required = FMath::Max(0.1f, GetDefault<USarkoRaidSettings>()->ExtractDwellSeconds);
	const float Left = FMath::Max(0.f, Required - Pawn->ExtractDwellSeconds);
	const FString Text = FString::Printf(TEXT("%s — %.1f"), *ZoneNameFor(Pawn->ExtractZoneIndex), Left);

	// Top-centre, below the clock and the loot prompt's slot: everything
	// informational lives along the top (spec §9), and never a bottom corner.
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, GEngine->GetLargeFont(), 1.5f);
	const float X = (Canvas->SizeX - Width) * 0.5f;
	constexpr float Y = 130.f;
	DrawRect(FLinearColor(0.f, 0.25f, 0.05f, 0.55f), X - 14.f, Y - 6.f, Width + 28.f, Height + 12.f);
	DrawText(Text, FLinearColor(0.55f, 1.f, 0.6f), X, Y, GEngine->GetLargeFont(), 1.5f);
}

void ASarkoHUD::DrawOutcomeSummary()
{
	const ASarkoRaidGameState* RaidState = GetWorld() ? GetWorld()->GetGameState<ASarkoRaidGameState>() : nullptr;
	if (!RaidState || !RaidState->IsRaidFinished())
	{
		return;
	}

	FString Title;
	FLinearColor Colour;
	switch (RaidState->Outcome)
	{
	case ESarkoRaidOutcome::Extracted: Title = TEXT("EXTRACTED"); Colour = FLinearColor(0.4f, 1.f, 0.45f); break;
	case ESarkoRaidOutcome::MIA:       Title = TEXT("MIA");       Colour = FLinearColor(1.f, 0.65f, 0.2f);  break;
	default:                           Title = TEXT("KIA");       Colour = FLinearColor(1.f, 0.25f, 0.2f);  break;
	}

	// Dim the world so the summary is unmistakably a final screen rather than
	// another HUD element.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	float TitleWidth = 0.f;
	float TitleHeight = 0.f;
	GetTextSize(Title, TitleWidth, TitleHeight, GEngine->GetLargeFont(), 2.5f);
	float Y = Canvas->SizeY * 0.22f;
	DrawText(Title, Colour, (Canvas->SizeX - TitleWidth) * 0.5f, Y, GEngine->GetLargeFont(), 2.5f);
	Y += TitleHeight + 24.f;

	// The itemised haul used to be drawn here and now lives in the shelter (spec
	// §6.5: "вынесено: …" moves there, where it sits above the stash it was just
	// credited into and can be compared with it). What stays is the outcome itself,
	// in the place the raid ended, plus a line saying where the player is about to
	// go — without it, PostRaidReturnSeconds of a dimmed frozen world reads as a
	// hang rather than as a beat.
	//
	// Keyed on bReturningToShelter, not on the outcome: ASarkoRaidGameMode::
	// ReturnToShelter returns without scheduling anything when there is no
	// USarkoGameInstance, and then the dimmed world really is where the player
	// stays. Promising a return that is not coming is the one thing this line must
	// not do, so the stuck case says so instead — the Error log explains it, and
	// this tells the player to go look.
	const FString Returning = RaidState->bReturningToShelter
		? TEXT("ПОВЕРНЕННЯ ДО УКРИТТЯ...")
		: TEXT("ПОВЕРНЕННЯ НЕДОСТУПНЕ — ДИВ. ЛОГ");
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Returning, Width, Height, GEngine->GetLargeFont(), 1.f);
	const FLinearColor ReturningColour = RaidState->bReturningToShelter
		? FLinearColor(0.8f, 0.8f, 0.8f)
		: FLinearColor(1.f, 0.65f, 0.2f);
	DrawText(Returning, ReturningColour, (Canvas->SizeX - Width) * 0.5f, Y,
		GEngine->GetLargeFont(), 1.f);
}
