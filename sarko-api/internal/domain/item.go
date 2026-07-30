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

// Caps on a single client-supplied stack list. Everything the client sends —
// a loadout, a raid result — is eventually accumulated into stash_items, so
// these are the bounds that keep invented data and runaway arithmetic out of
// the stash rather than merely out of one request.
const (
	// MaxItemIDLen bounds an item id. Real ids come from items.csv and are far
	// shorter; anything longer is an invented id, not a typo.
	MaxItemIDLen = 64
	// MaxStackQuantity bounds one stack. stash_items.quantity is a 32-bit INT
	// and credits accumulate across raids, so an unbounded per-request
	// quantity is an overflow waiting to happen.
	MaxStackQuantity = 1_000_000
	// MaxStacks bounds how many stacks one request may carry, so a single
	// valid-looking body cannot turn into thousands of statements.
	MaxStacks = 64
)

// ValidateStacks rejects malformed client input. An empty list is valid —
// entering a raid with nothing is a legitimate choice.
func ValidateStacks(stacks []ItemStack) error {
	if len(stacks) > MaxStacks {
		return fmt.Errorf("at most %d item stacks allowed, got %d", MaxStacks, len(stacks))
	}
	for _, s := range stacks {
		if s.ItemID == "" {
			return fmt.Errorf("item_id must not be empty")
		}
		// The offending id is never echoed back: it is unbounded attacker input.
		if len(s.ItemID) > MaxItemIDLen {
			return fmt.Errorf("item_id must be at most %d characters, got %d", MaxItemIDLen, len(s.ItemID))
		}
		if s.Quantity <= 0 {
			return fmt.Errorf("item %s: quantity must be positive, got %d", s.ItemID, s.Quantity)
		}
		if s.Quantity > MaxStackQuantity {
			return fmt.Errorf("item %s: quantity must be at most %d, got %d", s.ItemID, MaxStackQuantity, s.Quantity)
		}
	}
	return nil
}
