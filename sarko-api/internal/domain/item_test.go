package domain

import (
	"fmt"
	"strings"
	"testing"
)

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

	if len(first) != len(second) {
		t.Fatalf("length differs between calls: %d vs %d", len(first), len(second))
	}

	for i := range first {
		if first[i] != second[i] {
			t.Fatalf("order differs between calls: %v vs %v", first, second)
		}
	}
	if first[0].ItemID != "bolt" {
		t.Errorf("first item = %s, want bolt (sorted)", first[0].ItemID)
	}
}

// repeatStacks builds n distinct stacks, so the list-length cap is exercised
// without also tripping any per-stack rule.
func repeatStacks(n int) []ItemStack {
	out := make([]ItemStack, n)
	for i := range out {
		out[i] = ItemStack{ItemID: fmt.Sprintf("item-%d", i), Quantity: 1}
	}
	return out
}

func TestValidateStacksRejectsBadInput(t *testing.T) {
	cases := []struct {
		name   string
		stacks []ItemStack
	}{
		{"zero quantity", []ItemStack{{ItemID: "bolt", Quantity: 0}}},
		{"negative quantity", []ItemStack{{ItemID: "bolt", Quantity: -1}}},
		{"empty item id", []ItemStack{{ItemID: "", Quantity: 1}}},
		// An id longer than any real items.csv entry is invented data, and
		// nothing downstream bounds what it writes into stash_items.item_id.
		{"item id over the cap", []ItemStack{
			{ItemID: strings.Repeat("a", MaxItemIDLen+1), Quantity: 1}}},
		// stash_items.quantity is a 32-bit INT and credits accumulate, so an
		// unbounded per-request quantity overflows it after few enough raids.
		{"quantity over the cap", []ItemStack{
			{ItemID: "bolt", Quantity: MaxStackQuantity + 1}}},
		{"list longer than the cap", repeatStacks(MaxStacks + 1)},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if err := ValidateStacks(tc.stacks); err == nil {
				t.Error("expected error, got nil")
			}
		})
	}
}

// TestValidateStacksAcceptsTheBoundary pins the caps as inclusive, so a
// tightening typo (>= instead of >) is caught rather than silently rejecting
// legitimate play.
func TestValidateStacksAcceptsTheBoundary(t *testing.T) {
	cases := []struct {
		name   string
		stacks []ItemStack
	}{
		{"item id exactly at the cap", []ItemStack{
			{ItemID: strings.Repeat("a", MaxItemIDLen), Quantity: 1}}},
		{"quantity exactly at the cap", []ItemStack{
			{ItemID: "bolt", Quantity: MaxStackQuantity}}},
		{"list exactly at the cap", repeatStacks(MaxStacks)},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if err := ValidateStacks(tc.stacks); err != nil {
				t.Errorf("boundary value must be accepted: %v", err)
			}
		})
	}
}

// TestValidateStacksDoesNotEchoAnOversizedItemID keeps unbounded client input
// out of the error envelope: the message is written straight into a 400 body,
// so echoing the id would let a caller reflect arbitrary bytes back.
func TestValidateStacksDoesNotEchoAnOversizedItemID(t *testing.T) {
	huge := strings.Repeat("z", 5000)
	err := ValidateStacks([]ItemStack{{ItemID: huge, Quantity: 1}})
	if err == nil {
		t.Fatal("expected an error for an oversized item_id")
	}
	if strings.Contains(err.Error(), huge) {
		t.Errorf("error message echoes the oversized item_id: %q", err.Error())
	}
}

func TestValidateStacksAcceptsEmpty(t *testing.T) {
	if err := ValidateStacks(nil); err != nil {
		t.Errorf("empty loadout must be allowed: %v", err)
	}
}
