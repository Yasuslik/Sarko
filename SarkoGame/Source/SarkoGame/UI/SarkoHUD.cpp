#include "UI/SarkoHUD.h"

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
