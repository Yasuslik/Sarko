#include "UI/SarkoCombatFeedback.h"

uint8 SarkoFeedback::YawToByte(float YawDegrees)
{
	// Wrapped into [0, 360) FIRST, so -0.5 and 359.5 quantise to the same byte:
	// a shooter directly behind the victim is the commonest case there is, and it
	// sits exactly on the wrap.
	const float Wrapped = FMath::Fmod(FMath::Fmod(YawDegrees, 360.f) + 360.f, 360.f);
	// FloorToInt and a modulo rather than RoundToInt: rounding 359.9 gives 256,
	// which truncates to 0 by accident rather than by rule. This lands it there
	// on purpose.
	return static_cast<uint8>(FMath::FloorToInt(Wrapped * 256.f / 360.f) % 256);
}

float SarkoFeedback::ByteToYaw(uint8 Quantised)
{
	// The centre of the bucket, not its edge: decoding to the edge biases every
	// arc half a bucket clockwise, which is small, systematic, and exactly the
	// kind of thing that survives a decade in a codebase.
	return (static_cast<float>(Quantised) + 0.5f) * 360.f / 256.f;
}

float SarkoFeedback::FadeAlpha(float AgeSeconds, float LifetimeSeconds)
{
	if (LifetimeSeconds <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(1.f - AgeSeconds / LifetimeSeconds, 0.f, 1.f);
}

float SarkoFeedback::VignetteIntensity(float Health, float ThresholdHealth)
{
	if (ThresholdHealth <= 0.f)
	{
		return 0.f;
	}
	if (Health >= ThresholdHealth)
	{
		return 0.f;
	}
	return FMath::Clamp(1.f - FMath::Max(0.f, Health) / ThresholdHealth, 0.f, 1.f);
}

float SarkoFeedback::ArcSegmentAngle(float CentreRadians, float SpanRadians, int32 Index, int32 Count)
{
	if (Count <= 0)
	{
		return CentreRadians;
	}
	const float Clamped = static_cast<float>(FMath::Clamp(Index, 0, Count));
	return CentreRadians - SpanRadians * 0.5f + SpanRadians * (Clamped / static_cast<float>(Count));
}

void SarkoFeedback::FDamageArcRing::Add(float YawDegrees, float StartSeconds)
{
	Arcs[WriteIndex].YawDegrees = YawDegrees;
	Arcs[WriteIndex].StartSeconds = StartSeconds;
	WriteIndex = (WriteIndex + 1) % MaxDamageArcs;
	Count = FMath::Min(Count + 1, MaxDamageArcs);
}

void SarkoFeedback::FDamageArcRing::Reset()
{
	WriteIndex = 0;
	Count = 0;
}
