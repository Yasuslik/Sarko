#include "UI/SarkoHUD.h"

#include "Combat/SarkoWeapon.h"
#include "Core/SarkoPlayerController.h"
#include "Core/SarkoRaidGameState.h"
#include "Core/SarkoRaidSettings.h"
#include "Engine/Canvas.h"
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

	const int32 Total = FMath::CeilToInt(RaidState->RemainingSeconds);
	const FString Clock = FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);

	float OutWidth = 0.f;
	float OutHeight = 0.f;
	GetTextSize(Clock, OutWidth, OutHeight, GEngine->GetLargeFont(), 1.f);
	DrawText(Clock, FLinearColor::White, (Canvas->SizeX - OutWidth) * 0.5f, 24.f, GEngine->GetLargeFont(), 1.f);
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
	const FString AmmoText = bReloading ? TEXT("RELOADING") : FString::FromInt(Weapon->GetAmmoInMagazine());
	const FLinearColor Colour = bReloading ? FLinearColor(1.f, 0.6f, 0.1f, 1.f) : FLinearColor::White;

	DrawText(AmmoText, Colour, 24.f, 24.f, GEngine->GetLargeFont(), 1.f);
}
