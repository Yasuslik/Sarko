package domain

import "testing"

func TestUnlockedMapsGrowWithTier(t *testing.T) {
	none := UnlockedMaps(TierNone)
	if len(none) != 1 || none[0] != "forest" {
		t.Fatalf("TierNone = %v, want [forest]", none)
	}

	heli := UnlockedMaps(TierHelicopter)
	if len(heli) != 5 {
		t.Errorf("TierHelicopter unlocks %d maps, want 5", len(heli))
	}

	// Progression must be cumulative: a car keeps the forest.
	car := UnlockedMaps(TierCar)
	found := false
	for _, m := range car {
		if m == "forest" {
			found = true
		}
	}
	if !found {
		t.Error("TierCar must still include forest")
	}
}

func TestNextTier(t *testing.T) {
	next, ok := NextTier(TierNone)
	if !ok || next != TierBicycle {
		t.Errorf("NextTier(none) = %v/%v, want bicycle/true", next, ok)
	}
	if _, ok := NextTier(TierHelicopter); ok {
		t.Error("helicopter is the last tier, want ok=false")
	}
}

func TestRecipeExistsForEveryBuildableTier(t *testing.T) {
	for _, tier := range []Tier{TierBicycle, TierMotorcycle, TierCar, TierHelicopter} {
		parts, ok := Recipe(tier)
		if !ok {
			t.Errorf("Recipe(%s) missing", tier)
			continue
		}
		if len(parts) == 0 {
			t.Errorf("Recipe(%s) is empty", tier)
		}
		if err := ValidateStacks(parts); err != nil {
			t.Errorf("Recipe(%s) invalid: %v", tier, err)
		}
	}
	if _, ok := Recipe(TierNone); ok {
		t.Error("TierNone must not be buildable")
	}
}
