#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SarkoNoise.generated.h"

/**
 * THE NOISE MODEL (spec §7). Stealth's verb.
 *
 * Before this, `ASarkoAIController::Tick` treated *proximity* as hearing: a
 * living player within EnemyHearingRadiusUU was "heard" whatever they were
 * doing, so there was no way to be quiet and no way to be loud — the only
 * variable was distance, which the player cannot spend. Hearing now keys off
 * events a pawn *emits*, and emitting is a consequence of what the player
 * chooses to do:
 *
 *   firing   -> Loud    (~2600 uu)  a shot is a decision with a bill
 *   running  -> Audible (~1100 uu)  crossing open ground fast costs you
 *   walking  -> Quiet   (~450 uu)   the stealth option, and it is real
 *   standing -> Silent  (no event)  nothing to hear at all
 *
 * OPENING A CRATE IS SILENT, deliberately and for now. The design study raised
 * container noise ("a searched crate should be heard") and it was DEFERRED
 * rather than forgotten: looting already costs LootChannelSeconds of standing
 * still, and making that also aggro the guard turns the one safe verb in the
 * game into a second gunshot. If you came here looking for where a crate makes
 * its noise — it does not, on purpose. Add a `Quiet` report in
 * ASarkoCharacter::OpenContainerFor when that decision is revisited.
 *
 * Everything in the namespace below is pure arithmetic — no world, no actors —
 * so the radius selection, the audibility test and the ring buffer's bounds are
 * unit tested without a running raid. The subsystem at the bottom is the only
 * part that needs a world, and all it owns is the buffer.
 */
namespace SarkoNoise
{
	/**
	 * How loud one event is, as a name rather than a number. The numbers live in
	 * USarkoRaidSettings so they can be tuned from the ini like every other
	 * balance value; this enum is the vocabulary the code speaks.
	 */
	enum class EKind : uint8
	{
		/** No event is emitted at all. Standing still cannot be heard. */
		Silent,
		/** Walking. */
		Quiet,
		/** Running. */
		Audible,
		/** A shot. */
		Loud
	};

	/**
	 * How many events the world remembers at once.
	 *
	 * A fixed array and never a TArray: this is written from a tick path (the
	 * player's throttled movement report) and read from every bot's tick, and a
	 * per-tick heap allocation is exactly what this project forbids. Sixteen is
	 * far more than one raid can produce inside NoiseEventLifetimeSeconds — the
	 * player emits at most one movement event per NoiseMovementIntervalSeconds
	 * and one per shot, and four bots fire at most every FireIntervalSeconds —
	 * so the ring never actually wraps in play. It is a bound, not a budget.
	 */
	constexpr int32 MaxLiveEvents = 16;

	/** One thing that was heard: where, how far it carries, and when. */
	struct FNoiseEvent
	{
		FVector Location = FVector::ZeroVector;

		/** Base radius before the listener's own sensitivity is applied. */
		float RadiusUU = 0.f;

		/** World seconds. Compared against a lifetime rather than ticked down. */
		float TimeSeconds = -1000.f;

		/**
		 * Who made it. Weak so an expired corpse cannot keep an actor alive, and
		 * carried at all for one reason: a bot must not investigate its own
		 * gunshot. Without it, the first shot a scav fires sends it walking to
		 * where it is already standing.
		 */
		TWeakObjectPtr<const AActor> Instigator;
	};

	/**
	 * Pure: what a pawn moving at this speed sounds like.
	 *
	 * Speed rather than stick deflection, and that is the whole reason this is
	 * server-known: the move stick's deflection already scales speed
	 * (SarkoAim::MoveIntentScale feeds AddMovementInput's scale), so the fraction
	 * of MaxWalkSpeed a pawn is actually travelling at IS the deflection — and
	 * the server reads it off its own copy of the pawn's velocity, which no
	 * client can lie about. A stick value would have to be replicated and trusted.
	 *
	 * MoveFraction separates standing from walking and exists for the same reason
	 * MoveStickDeadZone does: a pawn decelerating to a stop, or drifting on a
	 * slope, must not go on shouting. RunFraction is the threshold the player
	 * feels — push the stick past it and the radius more than doubles.
	 *
	 * A non-positive MaxSpeed reads as Silent rather than dividing by zero.
	 */
	EKind KindForSpeed(float Speed2D, float MaxSpeed, float MoveFraction, float RunFraction);

	/** Pure: the base radius for a kind. Silent is exactly zero, which is what
	 *  makes "do not emit" a fact about the number rather than a caller's habit. */
	float RadiusForKind(EKind Kind, float QuietUU, float AudibleUU, float LoudUU);

	/**
	 * Pure: does this listener hear an event of this radius at this distance?
	 *
	 * Sensitivity is the LISTENER's multiplier (FSarkoBotArchetype::
	 * HearingSensitivity) — the scout hears further than the scav because the
	 * scout is a better listener, not because the world is louder near it. A
	 * non-positive sensitivity is a deaf bot rather than an infinitely sharp one.
	 */
	bool IsAudible(float DistanceUU, float EventRadiusUU, float Sensitivity);

	/**
	 * The bounded event buffer. Fixed storage, no allocation, oldest overwritten.
	 *
	 * A plain struct rather than part of the subsystem so the two properties
	 * worth asserting — it never grows past MaxLiveEvents, and it keeps the
	 * newest events rather than the oldest — are testable with no world at all.
	 */
	struct FNoiseRing
	{
		/** Overwrites the oldest slot once full. Never allocates. */
		void Add(const FNoiseEvent& Event);

		/**
		 * The most recent still-live event this listener can hear, or false.
		 *
		 * Most recent and not nearest: a bot that has just heard a shot should
		 * walk to the shot, not to the older footstep it is standing closer to.
		 * Ties (two events in the same frame) go to the louder one.
		 *
		 * IgnoreInstigator drops events this listener made itself. Pass null to
		 * hear everything, which is what the tests do.
		 */
		bool FindAudible(
			const FVector& ListenerLocation,
			float Sensitivity,
			float NowSeconds,
			float LifetimeSeconds,
			const AActor* IgnoreInstigator,
			FNoiseEvent& OutEvent) const;

		/** How many slots hold an event. Never exceeds MaxLiveEvents — that is the point. */
		int32 Num() const { return Count; }

		void Reset();

	private:
		FNoiseEvent Events[MaxLiveEvents];
		int32 WriteIndex = 0;
		int32 Count = 0;
	};
}

/**
 * Where noise events live for one world. Server-side by construction.
 *
 * A UWorldSubsystem rather than state on the game mode because the listeners are
 * AI controllers, which have no reason to know what a game mode is, and because
 * `World->GetSubsystem<>()` is a pointer lookup rather than a cast chain on a
 * path that runs once per bot per tick.
 *
 * Nothing here is replicated and nothing here may become replicated: "somebody
 * fired at (x, y)" is the server's model of what the AI knows, and a client that
 * received it would know where every other pawn is. Reports are refused off the
 * authority so a client cannot manufacture one either.
 */
UCLASS()
class USarkoNoiseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Server only: remember that something was heard here.
	 *
	 * Kind is turned into a radius here rather than by the caller, so the four
	 * numbers live in exactly one place and a caller cannot invent a fifth
	 * loudness. A Silent kind is dropped without touching the ring.
	 */
	void ReportNoise(const FVector& Location, SarkoNoise::EKind Kind, const AActor* Instigator);

	/** Convenience for the movement path: classifies the speed, then reports it. */
	void ReportMovementNoise(const FVector& Location, float Speed2D, float MaxSpeed, const AActor* Instigator);

	/**
	 * What this listener can hear right now. Reads the settings' lifetime so
	 * every caller ages events by the same clock.
	 */
	bool Hear(const FVector& ListenerLocation, float Sensitivity, const AActor* Listener,
		SarkoNoise::FNoiseEvent& OutEvent) const;

	/** Test seam: the ring, so its bounds can be asserted through the real type. */
	const SarkoNoise::FNoiseRing& GetRingForTest() const { return Ring; }

private:
	SarkoNoise::FNoiseRing Ring;
};
