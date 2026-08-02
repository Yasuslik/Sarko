#pragma once

#include "CoreMinimal.h"

/**
 * FEEDBACK WITHOUT SOUND (spec §4), as arithmetic.
 *
 * There is no audio stage yet, and until there is, a hit that lands produces
 * nothing at all: no marker, no flash, no flinch, no direction. These are the
 * bridge, and they are drawn — Canvas primitives and dynamic material instances
 * — because this project authors no assets and a post-process material is one.
 *
 * Everything here is pure so the parts that are easy to get quietly wrong are
 * unit tested: the yaw quantisation that carries a damage direction over the
 * wire in one byte, the arc's angle math, and the ring buffer's bounds. What
 * cannot be tested headlessly — whether the arc points at the shooter on a real
 * frame — is verified by a screenshot, because -nullrhi renders nothing.
 */
namespace SarkoFeedback
{
	/**
	 * A world yaw in one byte, and back.
	 *
	 * ONE BYTE because this rides along with damage on the health component
	 * rather than opening a channel of its own: health already replicates on
	 * exactly the events this describes, and a second reliable RPC per hit for a
	 * direction the player looks at for 0.6 s is not worth a packet.
	 *
	 * 360/256 = 1.4 degrees of error, against an arc that is forty-odd degrees
	 * wide — a third of one percent of the arc's own span, which is to say
	 * invisible. Wrapping is deliberate: 359.5 and -0.5 must land in the same
	 * place, because a shooter due north of the victim is the commonest case
	 * there is.
	 */
	uint8 YawToByte(float YawDegrees);
	float ByteToYaw(uint8 Quantised);

	/**
	 * Pure fade: 1 at the moment of the event, 0 at its end, and clamped at both
	 * ends so a stale entry can never draw and a clock that jumps backwards (a
	 * seamless travel, a loading hitch) cannot draw brighter than full.
	 */
	float FadeAlpha(float AgeSeconds, float LifetimeSeconds);

	/**
	 * Pure: how loud the low-health vignette should be, 0 at the threshold and 1
	 * at death.
	 *
	 * Zero ABOVE the threshold rather than a faint permanent tint: an effect that
	 * is always slightly on is an effect the player stops seeing, and the whole
	 * job of this one is to be noticed at the moment it appears.
	 */
	float VignetteIntensity(float Health, float ThresholdHealth);

	/**
	 * Pure: the angle of boundary `Index` of `Count` equal segments spanning
	 * SpanRadians, centred on CentreRadians.
	 *
	 * The arc is drawn as a short polyline around a circle centred on the pawn,
	 * so this is the whole of its geometry. Worth its own function because the
	 * failure it prevents is silent: an off-by-one in the boundary count draws an
	 * arc that is centred slightly off the direction it is reporting, and "the
	 * marker points a bit to the left of where I was shot from" is a bug nobody
	 * files and everybody feels.
	 */
	float ArcSegmentAngle(float CentreRadians, float SpanRadians, int32 Index, int32 Count);

	/** How many damage arcs may be on screen at once. */
	constexpr int32 MaxDamageArcs = 4;

	/** One arc: which way it points, and when it started. */
	struct FDamageArc
	{
		float YawDegrees = 0.f;
		float StartSeconds = -1000.f;
	};

	/**
	 * The arcs, in a fixed-size ring.
	 *
	 * Overlapping arcs are allowed on purpose — being shot by two people from two
	 * directions is exactly when the player most needs to be told — but they are
	 * BOUNDED, because this is written from a draw path and a TArray here would
	 * allocate during a firefight, which is the one moment it must not.
	 *
	 * Four, because a fifth simultaneous arc inside DamageArcSeconds means the
	 * player is dead and the summary screen is about to cover all of them anyway.
	 */
	struct FDamageArcRing
	{
		void Add(float YawDegrees, float StartSeconds);

		/** Fixed capacity, always. The one property worth asserting. */
		int32 Num() const { return Count; }
		const FDamageArc& Get(int32 Index) const { return Arcs[Index]; }

		void Reset();

	private:
		FDamageArc Arcs[MaxDamageArcs];
		int32 WriteIndex = 0;
		int32 Count = 0;
	};
}
