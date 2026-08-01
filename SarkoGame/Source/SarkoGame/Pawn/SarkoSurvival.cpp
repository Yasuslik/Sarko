#include "Pawn/SarkoSurvival.h"

#include "Core/SarkoRaidSettings.h"
#include "Engine/World.h"
#include "Loot/SarkoItemCatalog.h"
#include "Net/UnrealNetwork.h"
#include "Pawn/SarkoHealthComponent.h"

float SarkoSurvival::DrainMeter(float Current, float PerMinute, float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f)
	{
		return FMath::Clamp(Current, 0.f, MeterMax);
	}
	return FMath::Clamp(Current - PerMinute * (DeltaSeconds / 60.f), 0.f, MeterMax);
}

float SarkoSurvival::ApplyToMeter(float Current, float Amount)
{
	return FMath::Clamp(Current + Amount, 0.f, MeterMax);
}

float SarkoSurvival::RegenPerSecond(float BasePerSecond, float SecondsSinceCombat, float DelaySeconds,
	float FoodPercent, float WaterPercent, float LowPercent)
{
	if (BasePerSecond <= 0.f)
	{
		return 0.f;
	}
	// In combat, nothing. The medkit is the in-combat answer and this must never
	// compete with it — a bot's 21.5 hp/s would swamp it anyway, so a regen that
	// ran during a fight would be a number the player can neither see nor use.
	if (SecondsSinceCombat < DelaySeconds)
	{
		return 0.f;
	}

	const int32 LowMeters = (FoodPercent <= LowPercent ? 1 : 0) + (WaterPercent <= LowPercent ? 1 : 0);
	switch (LowMeters)
	{
	case 0:  return BasePerSecond;
	case 1:  return BasePerSecond * 0.5f;
	default: return 0.f;
	}
}

bool SarkoSurvival::ConsumableEffectFor(FName Item, FConsumeEffect& Out)
{
	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();

	// The category is the gate and the id is the effect. Both are checked: an
	// item outside the consumable category is not usable however it is spelled,
	// and a consumable with no row here is refused rather than given the average
	// effect of the ones that do have rows.
	const FSarkoItemDef* Def = SarkoLoot::GetItemCatalog().Find(Item);
	if (!Def || Def->Category != ESarkoItemCategory::Consumable)
	{
		return false;
	}

	Out = FConsumeEffect();
	if (Item == FName(TEXT("water_bottle")))
	{
		Out.Water = Settings.WaterBottleRestoresWater;
		return true;
	}
	if (Item == FName(TEXT("canned_food")))
	{
		Out.Food = Settings.CannedFoodRestoresFood;
		return true;
	}
	if (Item == FName(TEXT("vodka")))
	{
		// A small heal with a cost. See USarkoRaidSettings::VodkaHeals.
		Out.Heal = Settings.VodkaHeals;
		Out.Water = -Settings.VodkaCostsWater;
		return true;
	}
	return false;
}

USarkoSurvivalComponent::USarkoSurvivalComponent()
{
	// Ticking, but not per frame: the meters move by hundredths of a percent per
	// frame and the regeneration they gate is a slow trickle. The interval is
	// re-read from the settings in BeginPlay, where they are loaded.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
	SetIsReplicatedByDefault(true);
}

void USarkoSurvivalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Owner-only, for the reason the backpack is: how close a player is to their
	// penalty is exactly what makes them worth attacking.
	DOREPLIFETIME_CONDITION(USarkoSurvivalComponent, FoodPercent, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USarkoSurvivalComponent, WaterPercent, COND_OwnerOnly);
}

void USarkoSurvivalComponent::BeginPlay()
{
	Super::BeginPlay();

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	PrimaryComponentTick.TickInterval = FMath::Max(0.05f, Settings.SurvivalTickIntervalSeconds);

	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		// A client's copy is written by replication and by nothing else. Seeding
		// it here would make the HUD draw the starting values for one frame even
		// on a pawn that joined a raid already in progress.
		return;
	}

	Food = FMath::Clamp(Settings.FoodStartPercent, 0.f, SarkoSurvival::MeterMax);
	Water = FMath::Clamp(Settings.WaterStartPercent, 0.f, SarkoSurvival::MeterMax);
	PublishMeters();
}

void USarkoSurvivalComponent::ResetForTest(float NewFood, float NewWater)
{
	Food = FMath::Clamp(NewFood, 0.f, SarkoSurvival::MeterMax);
	Water = FMath::Clamp(NewWater, 0.f, SarkoSurvival::MeterMax);
	LastCombatSeconds = -1000.f;
	PublishMeters();
}

void USarkoSurvivalComponent::PublishMeters()
{
	// Rounded, and assigned unconditionally: replication itself only sends a
	// property that differs from the last one sent, so writing the same integer
	// back costs nothing on the wire. What this buys is that the wire ever sees
	// integers at all — a float would replicate on every tick forever.
	FoodPercent = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Food), 0, 100));
	WaterPercent = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Water), 0, 100));
}

bool USarkoSurvivalComponent::IsFoodLow() const
{
	return GetFoodPercent() <= GetDefault<USarkoRaidSettings>()->SurvivalLowPercent;
}

bool USarkoSurvivalComponent::IsWaterLow() const
{
	return GetWaterPercent() <= GetDefault<USarkoRaidSettings>()->SurvivalLowPercent;
}

void USarkoSurvivalComponent::NoteCombat()
{
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		return;
	}
	if (const UWorld* World = GetWorld())
	{
		LastCombatSeconds = World->GetTimeSeconds();
	}
}

void USarkoSurvivalComponent::TickComponent(float DeltaSeconds, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	// A corpse neither starves nor heals.
	const USarkoHealthComponent* Health = Owner->FindComponentByClass<USarkoHealthComponent>();
	if (Health && Health->IsDead())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	Food = SarkoSurvival::DrainMeter(Food, Settings.FoodDrainPerMinute, DeltaSeconds);
	Water = SarkoSurvival::DrainMeter(Water, Settings.WaterDrainPerMinute, DeltaSeconds);
	PublishMeters();

	TickRegen(DeltaSeconds);
}

void USarkoSurvivalComponent::TickRegen(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}
	USarkoHealthComponent* Health = Owner->FindComponentByClass<USarkoHealthComponent>();
	if (!Health || Health->IsDead() || Health->GetHealth() >= Health->GetMaxHealth())
	{
		return;
	}

	const USarkoRaidSettings& Settings = *GetDefault<USarkoRaidSettings>();
	const float PerSecond = SarkoSurvival::RegenPerSecond(
		Settings.HealthRegenPerSecond,
		World->GetTimeSeconds() - LastCombatSeconds,
		Settings.HealthRegenDelaySeconds,
		Food, Water, Settings.SurvivalLowPercent);
	// Logged when the RATE changes, not per tick. It changes when a fight ends,
	// when a meter crosses the threshold, and when the second one does — a
	// handful of lines in a fifteen-minute raid, and the only way a headless run
	// can show that regeneration is gated at all.
	if (!FMath::IsNearlyEqual(PerSecond, LastLoggedRegenPerSecond, 0.001f))
	{
		LastLoggedRegenPerSecond = PerSecond;
		UE_LOG(LogTemp, Display,
			TEXT("SarkoSurvival: out-of-combat regen now %.2f hp/s (food %.0f%%, water %.0f%%, %.1fs since combat, health %.0f)"),
			PerSecond, Food, Water, World->GetTimeSeconds() - LastCombatSeconds, Health->GetHealth());
	}

	if (PerSecond <= 0.f)
	{
		return;
	}
	// Heal clamps at MaxHealth itself, so an overshoot on the last tick of a
	// recovery is simply a full bar.
	Health->Heal(PerSecond * DeltaSeconds);
}

bool USarkoSurvivalComponent::ConsumeFromBag(TArray<FSarkoItemStack>& Bag, int32 SlotIndex)
{
	AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		return false;
	}
	// Hostile input: the index arrives from a client. Everything below indexes
	// only after this.
	if (!Bag.IsValidIndex(SlotIndex) || Bag[SlotIndex].Quantity <= 0)
	{
		return false;
	}

	SarkoSurvival::FConsumeEffect Effect;
	if (!SarkoSurvival::ConsumableEffectFor(Bag[SlotIndex].Item, Effect))
	{
		return false;
	}

	// The unit leaves the grid first, so a heal that somehow fails cannot leave
	// the player holding an item they have already drunk.
	if (--Bag[SlotIndex].Quantity <= 0)
	{
		Bag.RemoveAt(SlotIndex);
	}

	Food = SarkoSurvival::ApplyToMeter(Food, Effect.Food);
	Water = SarkoSurvival::ApplyToMeter(Water, Effect.Water);
	PublishMeters();

	if (Effect.Heal > 0.f)
	{
		if (USarkoHealthComponent* Health = Owner ? Owner->FindComponentByClass<USarkoHealthComponent>() : nullptr)
		{
			Health->Heal(Effect.Heal);
		}
	}
	return true;
}
