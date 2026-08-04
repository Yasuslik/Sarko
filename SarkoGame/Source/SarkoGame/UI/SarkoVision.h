#pragma once

#include "CoreMinimal.h"

/**
 * LIMITED VISION (spec docs/superpowers/specs/2026-08-04-sarko-vision-cone-design.md),
 * as arithmetic.
 *
 * The ask was «туман войны, типа область видимости — куда кручусь, туда и видно».
 * What that turns into is three states and not two:
 *
 *   - inside the cone AND with line of sight — full brightness, enemies drawn;
 *   - outside the cone, or behind cover      — the terrain stays VISIBLE, dimmed;
 *   - enemies                                — drawn only in the first case.
 *
 * **GEOMETRY IS NEVER HIDDEN, and that is the load-bearing decision in this
 * file.** There is no map and no compass in this game; a screen that goes black
 * behind the player leaves them lost rather than tense, and lost is not a
 * feeling anybody plays for. Dimming keeps the wall and the crate navigable and
 * still says "not now" — which is why every alpha below is a *tint* with a
 * ceiling (MaxDimAlpha) and why nothing here can ever return 1.
 *
 * Everything in this namespace is pure: no world, no actors, no canvas. The two
 * things that are easy to get quietly wrong — the angle predicate (a cone that
 * is off by the sign of a cross product points backwards, which is right half
 * the time and therefore the worst kind of wrong) and the edge ramp's monotonic
 * fall-off — are unit tested. What cannot be tested headlessly, i.e. whether the
 * dim level is playable on a phone in daylight, is verified by screenshots.
 */
namespace SarkoVision
{
	/**
	 * The cone's full angle, clamped to something a phone can be played with.
	 *
	 * WIDE ON PURPOSE. Human vision is about 120 degrees including periphery, and
	 * a narrow cone on a 6-inch screen does not read as tension — it reads as a
	 * rendering bug, and the player's first move is to look for the settings
	 * menu. The spec says start generous (110-120) and tune DOWN only if play
	 * says it is too easy.
	 *
	 * The floor is 40 degrees rather than 0: a cone narrower than that puts the
	 * pawn's own silhouette against the boundary at every step, and a setting
	 * that can be typed into an .ini must not be able to produce a game nobody
	 * can play. The ceiling is 350 rather than 360 because at 360 there is no
	 * cone at all and the whole feature silently switches itself off while still
	 * paying for its traces — if that is what is wanted, the honest switch is
	 * bVisionConeEnabled.
	 *
	 * HALF-ANGLES BELOW 90 ARE ALSO A DRAWING CONTRACT: the lit region is then
	 * the intersection of two half-planes, i.e. convex, which is what lets the
	 * dimming be a single fan of triangles with no self-overlap and therefore no
	 * seam where two translucent layers stack. Above 180 degrees of full angle it
	 * stops being convex; the fan still draws correctly (it is built in polar
	 * order, not by clipping), but nothing else in this project may assume
	 * convexity from it.
	 */
	constexpr float MinConeDegrees = 40.f;
	constexpr float MaxConeDegrees = 350.f;
	float ClampConeDegrees(float FullAngleDegrees);

	/** Half of the clamped full angle — the form every predicate below wants. */
	float ConeHalfAngleDegrees(float FullAngleDegrees);

	/**
	 * The soft edge, clamped. A hard wedge reads as a drawn triangle laid over
	 * the world; a few degrees of ramp reads as sight falling off, which is the
	 * thing being pictured.
	 *
	 * Bounded by the half-angle it hangs off, because a ramp wider than the cone
	 * would put the fully-lit core at zero degrees — a cone that is all edge is
	 * not a cone.
	 */
	constexpr float MaxSoftEdgeDegrees = 40.f;
	float ClampSoftEdgeDegrees(float SoftEdgeDegrees, float HalfAngleDegrees);

	/**
	 * The dim level, clamped well away from 1.
	 *
	 * 0.85 IS A HARD CEILING AND NOT A DEFAULT. The spec's first named risk is
	 * "too dark is unplayable, especially on a phone in daylight", and the second
	 * is disorientation with no map and no compass. A tint the player can still
	 * navigate by is the entire mitigation for both, so the range itself refuses
	 * to express "blacked out" — an .ini cannot turn this feature into the thing
	 * the spec ruled out.
	 */
	constexpr float MaxDimAlpha = 0.85f;
	float ClampDimAlpha(float DimAlpha);

	/**
	 * Signed angle in DEGREES from Facing to Direction, in the ground plane.
	 *
	 * Both arguments are taken unnormalised and normalised here, because every
	 * caller has one of them from a replicated FVector_NetQuantizeNormal (which
	 * is only approximately unit after the wire) and the other from a subtraction
	 * of two world positions. Returns 0 for a degenerate input rather than a NaN:
	 * a pawn standing exactly on top of another is "in front of it", which is
	 * both harmless and true.
	 *
	 * Z IS DISCARDED. The camera is overhead and world-locked and every actor in
	 * this game stands on the same floor; a cone that narrowed as a bot walked up
	 * a ramp would be a bug nobody could explain.
	 */
	float SignedAngleDegrees(FVector2D Facing, FVector2D Direction);

	/**
	 * The angle predicate: is Direction inside a cone of HalfAngleDegrees about
	 * Facing?
	 *
	 * Half of the whole visibility rule. The other half is line of sight, which
	 * needs a world and is therefore the caller's — see
	 * ASarkoRaidGameMode::UpdateEnemyVisibility, which is the ONE place that
	 * decides who is drawn, and does it on the server.
	 */
	bool IsInsideCone(FVector2D Facing, FVector2D Direction, float HalfAngleDegrees);

	/**
	 * The whole visibility rule, with the world's answer passed in.
	 *
	 * Written as one function rather than as `IsInsideCone(...) && bHasLos` at
	 * each call site so that "what does it take to be seen" is a single testable
	 * statement, and so the RANGE bound cannot be forgotten by one caller and
	 * remembered by another. A non-positive RangeUU means unlimited, which is
	 * what a top-down camera wants: anything the player can see on screen at all
	 * is inside a couple of thousand uu.
	 */
	bool IsVisible(FVector2D Facing, FVector2D ToTarget, float HalfAngleDegrees,
		bool bHasLineOfSight, float RangeUU);

	/**
	 * How much to DIM a point at AngleFromCentreDegrees off the cone's axis.
	 *
	 * Zero inside the core, MaxAlpha outside the ramp, and a STEPPED ramp
	 * between: quantised into EdgeSteps plateaus rather than a continuous slope,
	 * because the drawing samples this at a handful of angles and interpolates
	 * between the samples — a plateau either side of every sample is what makes
	 * that interpolation land on the ramp this function describes instead of on a
	 * straight line through it.
	 *
	 * Monotonic non-decreasing in the angle, always. That is the property worth
	 * asserting: a ramp with a dip in it draws a bright ring outside the cone,
	 * which reads as a second cone that is not there.
	 *
	 * The angle is taken as a magnitude — the cone is symmetric about its axis
	 * and nothing in the game distinguishes its left edge from its right.
	 */
	constexpr int32 EdgeSteps = 4;
	float DimAlphaForAngle(float AngleFromCentreDegrees, float HalfAngleDegrees,
		float SoftEdgeDegrees, float MaxAlpha);

	/**
	 * The near halo: how much of the dim survives at RadiusUU from the pawn.
	 *
	 * ONE AT THE PAWN'S OWN FEET IS WRONG. The cone's apex is the pawn, and a fan
	 * of triangles meeting at a single point has one colour there — so without
	 * this the player's own body, the crate they are standing next to and the
	 * ground under both are drawn at the full dim on three sides of them, which
	 * is the one place a player must never have to guess about. It is also what a
	 * body actually knows: you do not need to be looking at your own feet to know
	 * what is under them.
	 *
	 * Deliberately SMALL (a couple of metres, set by the caller) and a ramp
	 * rather than a disc, so it reads as the cone's apex softening and not as a
	 * second, circular vision radius that would undo the whole feature.
	 *
	 * Returns a SCALE in [0,1] applied to the dim, so 0 is fully lit.
	 */
	float NearHaloScale(float RadiusUU, float HaloRadiusUU);
}
