package domain

import "testing"

func TestMergeStacksCombinesSameItem(t *testing.T) {
	got := MergeStacks([]ItemStack{
		{ItemID: "bolt", Quantity: 2},
		{ItemID: "chain", Quantity: 1},
		{ItemID: "bolt", Quantity: 3},
	})

	want := map[string]int{"bolt": 5, "chain": 1}
	if len(got) != len(want) {
		t.Fatalf("len = %d, want %d (%v)", len(got), len(want), got)
	}
	for _, s := range got {
		if want[s.ItemID] != s.Quantity {
			t.Errorf("%s = %d, want %d", s.ItemID, s.Quantity, want[s.ItemID])
		}
	}
}

func TestMergeStacksIsDeterministic(t *testing.T) {
	in := []ItemStack{{ItemID: "wheel", Quantity: 1}, {ItemID: "bolt", Quantity: 1}}
	first := MergeStacks(in)
	second := MergeStacks(in)

	for i := range first {
		if first[i] != second[i] {
			t.Fatalf("order differs between calls: %v vs %v", first, second)
		}
	}
	if first[0].ItemID != "bolt" {
		t.Errorf("first item = %s, want bolt (sorted)", first[0].ItemID)
	}
}

func TestValidateStacksRejectsBadInput(t *testing.T) {
	cases := []struct {
		name   string
		stacks []ItemStack
	}{
		{"zero quantity", []ItemStack{{ItemID: "bolt", Quantity: 0}}},
		{"negative quantity", []ItemStack{{ItemID: "bolt", Quantity: -1}}},
		{"empty item id", []ItemStack{{ItemID: "", Quantity: 1}}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if err := ValidateStacks(tc.stacks); err == nil {
				t.Error("expected error, got nil")
			}
		})
	}
}

func TestValidateStacksAcceptsEmpty(t *testing.T) {
	if err := ValidateStacks(nil); err != nil {
		t.Errorf("empty loadout must be allowed: %v", err)
	}
}
