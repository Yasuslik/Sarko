#include "AI/SarkoNoise.h"

#include "Core/SarkoRaidSettings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

SarkoNoise::EKind SarkoNoise::KindForSpeed(float Speed2D, float MaxSpeed, float MoveFraction, float RunFraction)
{
	// A pawn with no speed of its own cannot be classified against a fraction of
	// it. Silent rather than Loud: an unknown must never be the noisy answer.
	if (MaxSpeed <= 0.f || Speed2D <= 0.f)
	{
		return EKind::Silent;
	}

	const float Fraction = Speed2D / MaxSpeed;
	if (Fraction < FMath::Max(0.f, MoveFraction))
	{
		return EKind::Silent;
	}
	// The boundary belongs to running, for the same reason SarkoExtract::
	// IsZoneOpen's belongs to open: a threshold the player is standing exactly on
	// and that refuses is a bug report.
	return Fraction >= FMath::Max(0.f, RunFraction) ? EKind::Audible : EKind::Quiet;
}

float SarkoNoise::RadiusForKind(EKind Kind, float QuietUU, float AudibleUU, float LoudUU)
{
	switch (Kind)
	{
	case EKind::Quiet:   return FMath::Max(0.f, QuietUU);
	case EKind::Audible: return FMath::Max(0.f, AudibleUU);
	case EKind::Loud:    return FMath::Max(0.f, LoudUU);
	default:             return 0.f;
	}
}

bool SarkoNoise::IsAudible(float DistanceUU, float EventRadiusUU, float Sensitivity)
{
	if (EventRadiusUU <= 0.f || Sensitivity <= 0.f)
	{
		return false;
	}
	// Inclusive, and squared nowhere: the caller already has a real distance and
	// this is one comparison per event per bot per tick.
	return DistanceUU <= EventRadiusUU * Sensitivity;
}

void SarkoNoise::FNoiseRing::Add(const FNoiseEvent& Event)
{
	Events[WriteIndex] = Event;
	WriteIndex = (WriteIndex + 1) % MaxLiveEvents;
	Count = FMath::Min(Count + 1, MaxLiveEvents);
}

void SarkoNoise::FNoiseRing::Reset()
{
	WriteIndex = 0;
	Count = 0;
}

bool SarkoNoise::FNoiseRing::FindAudible(
	const FVector& ListenerLocation,
	float Sensitivity,
	float NowSeconds,
	float LifetimeSeconds,
	const AActor* IgnoreInstigator,
	FNoiseEvent& OutEvent) const
{
	bool bFound = false;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FNoiseEvent& Candidate = Events[Index];

		// Expired. Aged against a lifetime rather than swept out of the ring, so
		// nothing has to tick to keep the buffer honest.
		if (NowSeconds - Candidate.TimeSeconds > LifetimeSeconds)
		{
			continue;
		}
		// A bot investigating its own gunshot walks to where it is standing and
		// looks, for AIInvestigateTimeoutSeconds, like a bot that has broken.
		if (IgnoreInstigator && Candidate.Instigator.Get() == IgnoreInstigator)
		{
			continue;
		}
		if (!IsAudible(FVector::Dist(ListenerLocation, Candidate.Location), Candidate.RadiusUU, Sensitivity))
		{
			continue;
		}

		// Newest wins; a tie goes to the louder event. Both halves matter: a shot
		// and a footstep can land in the same frame, and the shot is the one the
		// bot should walk to.
		if (!bFound
			|| Candidate.TimeSeconds > OutEvent.TimeSeconds
			|| (Candidate.TimeSeconds == OutEvent.TimeSeconds && Candidate.RadiusUU > OutEvent.RadiusUU))
		{
			OutEvent = Candidate;
			bFound = true;
		}
	}
	return bFound;
}

void USarkoNoiseSubsystem::ReportNoise(const FVector& Location, SarkoNoise::EKind Kind, const AActor* Instigator)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// THE SERVER'S MODEL, and only the server's. A client has no bots to feed and
	// nothing here replicates, so a report off the authority is either a bug or a
	// client trying to move somebody else's AI.
	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float Radius = SarkoNoise::RadiusForKind(Kind,
		Settings.NoiseQuietRadiusUU, Settings.NoiseAudibleRadiusUU, Settings.NoiseLoudRadiusUU);
	if (Radius <= 0.f)
	{
		// Standing still. Nothing is stored, so nothing can be heard — silence is
		// the absence of an event rather than an event with a small radius.
		return;
	}

	SarkoNoise::FNoiseEvent Event;
	Event.Location = Location;
	Event.RadiusUU = Radius;
	Event.TimeSeconds = World->GetTimeSeconds();
	Event.Instigator = Instigator;
	Ring.Add(Event);

	if (Settings.bLogAIDiagnostics)
	{
		UE_LOG(LogTemp, Log, TEXT("SarkoNoise: %s event at %s, radius %.0f uu, from %s"),
			Kind == SarkoNoise::EKind::Loud ? TEXT("LOUD") : (Kind == SarkoNoise::EKind::Audible ? TEXT("audible") : TEXT("quiet")),
			*Location.ToString(), Radius, Instigator ? *Instigator->GetName() : TEXT("nobody"));
	}
}

void USarkoNoiseSubsystem::ReportMovementNoise(const FVector& Location, float Speed2D, float MaxSpeed, const AActor* Instigator)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	ReportNoise(Location,
		SarkoNoise::KindForSpeed(Speed2D, MaxSpeed, Settings.NoiseMoveSpeedFraction, Settings.NoiseRunSpeedFraction),
		Instigator);
}

bool USarkoNoiseSubsystem::Hear(const FVector& ListenerLocation, float Sensitivity, const AActor* Listener,
	SarkoNoise::FNoiseEvent& OutEvent) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	return Ring.FindAudible(ListenerLocation, Sensitivity, World->GetTimeSeconds(),
		GetDefault<USarkoRaidSettings>()->NoiseEventLifetimeSeconds, Listener, OutEvent);
}
