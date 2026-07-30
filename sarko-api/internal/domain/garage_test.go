package domain

import "testing"

func TestUnlockedMapsGrowWithTier(t *testing.T) {
	none := UnlockedMaps(TierNone)
	if len(none) != 1 || none[0] != "bridge" {
		t.Fatalf("TierNone = %v, want [bridge]", none)
	}

	heli := UnlockedMaps(TierHelicopter)
	if len(heli) != 5 {
		t.Errorf("TierHelicopter unlocks %d maps, want 5", len(heli))
	}

	// Progression must be cumulative: a car keeps the bridge.
	car := UnlockedMaps(TierCar)
	found := false
	for _, m := range car {
		if m == "bridge" {
			found = true
		}
	}
	if !found {
		t.Error("TierCar must still include bridge")
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

func TestRecipeReturnsCopy(t *testing.T) {
	// First call: get a recipe and mutate it
	first, ok := Recipe(TierBicycle)
	if !ok {
		t.Fatal("Recipe(TierBicycle) should exist")
	}
	if len(first) == 0 {
		t.Fatal("Recipe(TierBicycle) is empty")
	}

	// Mutate the returned slice
	originalQuantity := first[0].Quantity
	first[0].Quantity = 999

	// Second call: get the same recipe again
	second, ok := Recipe(TierBicycle)
	if !ok {
		t.Fatal("Recipe(TierBicycle) should still exist")
	}

	// The second call must be unaffected by the mutation
	if second[0].Quantity != originalQuantity {
		t.Errorf("Recipe result is not a copy: first mutation affected second call. "+
			"first[0].Quantity = %d, second[0].Quantity = %d, want both = %d",
			first[0].Quantity, second[0].Quantity, originalQuantity)
	}
}

func TestIsValidTier(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want bool
	}{
		{"none", "none", true},
		{"bicycle", "bicycle", true},
		{"motorcycle", "motorcycle", true},
		{"car", "car", true},
		{"helicopter", "helicopter", true},
		{"empty string", "", false},
		{"bogus", "bogus", false},
		{"None capitalized", "None", false},
		{"random string", "tank", false},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := IsValidTier(tc.in)
			if got != tc.want {
				t.Errorf("IsValidTier(%q) = %v, want %v", tc.in, got, tc.want)
			}
		})
	}
}
