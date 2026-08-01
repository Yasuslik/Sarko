#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SarkoSurvival.generated.h"

namespace SarkoSurvival
{
	/** Both meters are percentages, and both ends of the range are real states. */
	constexpr float MeterMax = 100.f;

	/**
	 * One meter after DeltaSeconds of raid time. Pure.
	 *
	 * Clamped at both ends: hunger and thirst are never lethal in this slice
	 * (owner decision), so zero is a floor and not a death, and nothing may push
	 * a meter above full — a player who drinks two bottles keeps the second
	 * bottle's water only up to 100.
	 */
	float DrainMeter(float Current, float PerMinute, float DeltaSeconds);

	/** Current plus Amount, clamped into [0, MeterMax]. Pure. Used by every
	 *  consumable and by the negative half of vodka's trade. */
	float ApplyToMeter(float Current, float Amount);

	/**
	 * How fast health comes back right now, in hp per second. Pure, and the whole
	 * of what hunger and thirst DO.
	 *
	 * Three rules, in this order:
	 *  - in combat (fewer than DelaySeconds since the last damage taken OR dealt)
	 *    it is zero. Regen must never be felt inside a firefight — the medkit is
	 *    the in-combat answer and this must not compete with it;
	 *  - one meter at or below LowPercent halves it;
	 *  - both meters at or below LowPercent stop it entirely.
	 *
	 * That last step is the difference between a nuisance and a state: a player
	 * who has ignored both meters for fifteen minutes carries every wound home.
	 */
	float RegenPerSecond(float BasePerSecond, float SecondsSinceCombat, float DelaySeconds,
		float FoodPercent, float WaterPercent, float LowPercent);

	/** What one unit of an item does when it is used. Positive numbers give. */
	struct FConsumeEffect
	{
		float Food = 0.f;
		float Water = 0.f;
		float Heal = 0.f;
	};

	/**
	 * The effect table, keyed by item id. Returns false for anything that is not
	 * consumable, which is the server's last validation step and the panel's rule
	 * for which cells are tappable.
	 *
	 * Deliberately keyed on the ID and not only on the category: the category
	 * says "this can be used", the id says what using it DOES, and inventing a
	 * default effect for an unlisted consumable would be inventing balance. A new
	 * consumable with no row here is refused, loudly, rather than silently doing
	 * the average thing.
	 */
	bool ConsumableEffectFor(FName Item, FConsumeEffect& Out);
}

/**
 * Hunger and thirst, and the health regeneration they gate.
 *
 * Server-authoritative and replicated **owner-only**: how close another player
 * is to their penalty is exactly the sort of thing that decides whether they are
 * worth attacking, and it is the same rule the backpack already follows.
 *
 * Replicated as two uint8 percentages rather than floats, and they are written
 * from the float only when the ROUNDED value changes — thirst drains 3.3 per
 * minute, so that is one property update roughly every eighteen seconds against
 * one per tick. The float is the server's truth and never leaves it.
 *
 * Ticks on a fixed interval rather than per frame (see the constructor): meters
 * that move by a thousandth of a percent per frame have nothing to say sixty
 * times a second.
 */
UCLASS(ClassGroup = (Sarko), meta = (BlueprintSpawnableComponent))
class USarkoSurvivalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USarkoSurvivalComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** What the HUD draws. On the server these are the rounded truth; on the
	 *  owning client they are what replication last sent. */
	float GetFoodPercent() const { return static_cast<float>(FoodPercent); }
	float GetWaterPercent() const { return static_cast<float>(WaterPercent); }

	/** True while either meter is at or below the penalty threshold — what makes
	 *  the HUD bar say so rather than making the player do the arithmetic. */
	bool IsFoodLow() const;
	bool IsWaterLow() const;

	/**
	 * Server only: this pawn took or dealt damage just now, so regeneration is
	 * off for the next HealthRegenDelaySeconds.
	 *
	 * Both directions on purpose. "Since the last damage taken" alone would let a
	 * player who is winning a firefight regenerate through it while the bot is
	 * still shooting back and missing.
	 */
	void NoteCombat();

	/**
	 * Server only. Uses one unit of Item out of Bag, applies its effect, and
	 * returns true when something actually happened.
	 *
	 * Takes the bag by reference and writes the consumed stack back, so the whole
	 * transaction — the unit leaving the grid and the meter moving — is one place
	 * that cannot half-succeed. Every input is treated as hostile: the item must
	 * be in the pawn's own cells at the index given, and it must have a row in
	 * SarkoSurvival::ConsumableEffectFor.
	 */
	bool ConsumeFromBag(TArray<struct FSarkoItemStack>& Bag, int32 SlotIndex);

	/** Test seam: a known state without a world or a replication cycle. */
	void ResetForTest(float Food, float Water);

	/** Test seam: reads the server's float, not the replicated integer. */
	float GetFoodExact() const { return Food; }
	float GetWaterExact() const { return Water; }

private:
	/** Server truth. Floats, because the drain per tick is a fraction of a
	 *  percent and integers would round it away to nothing. */
	float Food = 0.f;
	float Water = 0.f;

	/**
	 * World time of the last damage this pawn took or dealt. Starts far enough in
	 * the past that a raid begins out of combat — a player who spawns at full
	 * health has nothing to regenerate anyway, so this only matters for a raid
	 * entered at less than full.
	 */
	float LastCombatSeconds = -1000.f;

	/** COND_OwnerOnly, uint8 percentages. See the class comment for why these are
	 *  not the floats above. */
	UPROPERTY(Replicated)
	uint8 FoodPercent = 0;

	UPROPERTY(Replicated)
	uint8 WaterPercent = 0;

	/** Writes the two replicated integers from the two floats, and only when the
	 *  rounded value has actually moved. */
	void PublishMeters();

	/** Server: runs the out-of-combat regeneration for this tick. */
	void TickRegen(float DeltaSeconds);
};
