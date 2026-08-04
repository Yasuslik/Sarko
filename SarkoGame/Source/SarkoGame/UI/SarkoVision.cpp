#include "UI/SarkoVision.h"

namespace SarkoVision
{
	float ClampConeDegrees(float FullAngleDegrees)
	{
		return FMath::Clamp(FullAngleDegrees, MinConeDegrees, MaxConeDegrees);
	}

	float ConeHalfAngleDegrees(float FullAngleDegrees)
	{
		return ClampConeDegrees(FullAngleDegrees) * 0.5f;
	}

	float ClampSoftEdgeDegrees(float SoftEdgeDegrees, float HalfAngleDegrees)
	{
		// Never wider than the half-angle it hangs off: a ramp that eats the whole
		// cone leaves no fully-lit core, and a cone that is all edge is a wash.
		const float Ceiling = FMath::Min(MaxSoftEdgeDegrees, FMath::Max(0.f, HalfAngleDegrees));
		return FMath::Clamp(SoftEdgeDegrees, 0.f, Ceiling);
	}

	float ClampDimAlpha(float DimAlpha)
	{
		return FMath::Clamp(DimAlpha, 0.f, MaxDimAlpha);
	}

	float SignedAngleDegrees(FVector2D Facing, FVector2D Direction)
	{
		const FVector2D F = Facing.GetSafeNormal();
		const FVector2D D = Direction.GetSafeNormal();
		if (F.IsNearlyZero() || D.IsNearlyZero())
		{
			// A pawn with no facing, or a target standing exactly on top of it.
			// "Straight ahead" is the answer that hides nothing, and hiding
			// something the player is touching is the failure worth avoiding.
			return 0.f;
		}

		// Atan2 of the cross against the dot, rather than an acos of the dot: acos
		// loses all sign (so a target dead astern reads as dead ahead) and is
		// numerically worst exactly at the two angles that matter most, 0 and 180.
		const float Cross = F.X * D.Y - F.Y * D.X;
		const float Dot = F.X * D.X + F.Y * D.Y;
		return FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}

	bool IsInsideCone(FVector2D Facing, FVector2D Direction, float HalfAngleDegrees)
	{
		return FMath::Abs(SignedAngleDegrees(Facing, Direction)) <= FMath::Max(0.f, HalfAngleDegrees);
	}

	bool IsVisible(FVector2D Facing, FVector2D ToTarget, float HalfAngleDegrees,
		bool bHasLineOfSight, float RangeUU)
	{
		if (!bHasLineOfSight)
		{
			return false;
		}
		// Non-positive is unlimited. Measured before the angle because it is the
		// cheaper of the two and rejects most of the map on a big level.
		if (RangeUU > 0.f && ToTarget.SizeSquared() > RangeUU * RangeUU)
		{
			return false;
		}
		return IsInsideCone(Facing, ToTarget, HalfAngleDegrees);
	}

	float DimAlphaForAngle(float AngleFromCentreDegrees, float HalfAngleDegrees,
		float SoftEdgeDegrees, float MaxAlpha)
	{
		const float Angle = FMath::Abs(AngleFromCentreDegrees);
		const float Half = FMath::Max(0.f, HalfAngleDegrees);
		const float Soft = ClampSoftEdgeDegrees(SoftEdgeDegrees, Half);
		const float Ceiling = ClampDimAlpha(MaxAlpha);

		if (Angle <= Half)
		{
			return 0.f;
		}
		if (Soft <= KINDA_SMALL_NUMBER || Angle >= Half + Soft)
		{
			return Ceiling;
		}

		// Quantised into EdgeSteps plateaus. CeilToInt rather than FloorToInt so
		// the first sample past the core is already a step down from full
		// brightness — a ramp whose first plateau is zero is a ramp that starts
		// one step late, and the boundary is then still a visible hard line.
		const float T = (Angle - Half) / Soft;
		const int32 Step = FMath::Clamp(FMath::CeilToInt(T * EdgeSteps), 1, EdgeSteps);
		return Ceiling * (static_cast<float>(Step) / static_cast<float>(EdgeSteps));
	}

	float NearHaloScale(float RadiusUU, float HaloRadiusUU)
	{
		if (HaloRadiusUU <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		return FMath::Clamp(RadiusUU / HaloRadiusUU, 0.f, 1.f);
	}
}
