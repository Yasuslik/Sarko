// Package domain holds Sarko's pure rules: no database, no HTTP, no clock.
package domain

import (
	"fmt"
	"sort"
)

// ItemStack is a quantity of one item id. Item ids match items.csv on the client.
type ItemStack struct {
	ItemID   string `json:"item_id"`
	Quantity int    `json:"quantity"`
}

// MergeStacks folds duplicate item ids together and returns them sorted by id,
// so equal inputs always produce byte-identical output.
func MergeStacks(stacks []ItemStack) []ItemStack {
	totals := make(map[string]int, len(stacks))
	for _, s := range stacks {
		totals[s.ItemID] += s.Quantity
	}

	out := make([]ItemStack, 0, len(totals))
	for id, qty := range totals {
		out = append(out, ItemStack{ItemID: id, Quantity: qty})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ItemID < out[j].ItemID })
	return out
}

// ValidateStacks rejects malformed client input. An empty list is valid —
// entering a raid with nothing is a legitimate choice.
func ValidateStacks(stacks []ItemStack) error {
	for _, s := range stacks {
		if s.ItemID == "" {
			return fmt.Errorf("item_id must not be empty")
		}
		if s.Quantity <= 0 {
			return fmt.Errorf("item %s: quantity must be positive, got %d", s.ItemID, s.Quantity)
		}
	}
	return nil
}
