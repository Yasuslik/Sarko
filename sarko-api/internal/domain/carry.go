package domain

import (
	"fmt"
	"sort"
)

// GridPage is one page of the carry grid, in whole cells.
type GridPage struct {
	Columns int
	Rows    int
}

// Cells is the page's area.
func (p GridPage) Cells() int { return p.Columns * p.Rows }

// CarryPages is everything a player can carry out of a raid, as the client
// models it: a 2×2 pocket page that is always there, plus the 4×2 page a worn
// backpack adds. It mirrors USarkoRaidSettings::PocketGrid and ::BackpackGrid
// (SarkoGame/Source/SarkoGame/Core/SarkoRaidSettings.h:127,137).
//
// The backpack page is unconditional here even though the client only shows it
// while a bag is worn. That is deliberate: this is a plausibility gate, and the
// question it answers is "could ANY legitimate raid have carried this?", whose
// answer is always measured against the best case — a player wearing a bag. A
// gate that also tried to decide whether the bag was worn would start rejecting
// real hauls the moment the client's idea of "worn" changed, and a rejected
// result is a player's whole raid deleted.
//
// 4 + 8 = 12 cells, which is exactly the count MaxRaidStacks is built on
// (12 cells + the worn bag itself = 13 stacks). Changing either without the
// other puts the two bounds into disagreement.
var CarryPages = []GridPage{
	{Columns: 2, Rows: 2},
	{Columns: 4, Rows: 2},
}

// WornBagID is the item a player wears rather than packs. One of them rides
// home without occupying a cell — that is the whole reason MaxRaidStacks is 13
// and not 12 — so exactly one is exempt from the placement below. A second
// backpack is cargo like anything else, and takes its 2×2.
const WornBagID = "backpack"

// TotalCarryCells is how many cells the full carry grid has. Used as a cheap
// pre-filter before the placer runs.
func TotalCarryCells() int {
	total := 0
	for _, p := range CarryPages {
		total += p.Cells()
	}
	return total
}

// carryRect is one stack's footprint, ready to place.
type carryRect struct {
	itemID string
	w, h   int
}

// occupancy is one bitmap per page, row-major, mirroring FOccupancy in
// SarkoGame/Source/SarkoGame/Loot/SarkoItemGrid.cpp.
type occupancy struct {
	pages []GridPage
	cells [][]bool
}

func newOccupancy(pages []GridPage) *occupancy {
	o := &occupancy{pages: pages, cells: make([][]bool, len(pages))}
	for i, p := range pages {
		o.cells[i] = make([]bool, p.Cells())
	}
	return o
}

func (o *occupancy) free(page, x, y, w, h int) bool {
	p := o.pages[page]
	if x < 0 || y < 0 || x+w > p.Columns || y+h > p.Rows {
		return false
	}
	for row := y; row < y+h; row++ {
		for col := x; col < x+w; col++ {
			if o.cells[page][row*p.Columns+col] {
				return false
			}
		}
	}
	return true
}

func (o *occupancy) occupy(page, x, y, w, h int) {
	p := o.pages[page]
	for row := y; row < y+h; row++ {
		for col := x; col < x+w; col++ {
			o.cells[page][row*p.Columns+col] = true
		}
	}
}

// firstFit is SarkoGrid::FirstFit: every page in order, and within a page rows
// outer / columns inner — "left to right, top to bottom", starting from (0,0)
// every time so the scan backfills a hole a wider item skipped over. No
// rotation. It returns whether the rectangle was placed.
func (o *occupancy) firstFit(w, h int) bool {
	for page, p := range o.pages {
		for y := 0; y+h <= p.Rows; y++ {
			for x := 0; x+w <= p.Columns; x++ {
				if o.free(page, x, y, w, h) {
					o.occupy(page, x, y, w, h)
					return true
				}
			}
		}
	}
	return false
}

// carryRects turns merged stacks into the rectangles they occupy: a stack is
// one rectangle whatever its count (a 60-round stack of 9×18 is one cell, the
// same as a 1-round stack), so a quantity becomes ceil(quantity / stackSize)
// rectangles of that item's size.
//
// Unknown ids are skipped rather than guessed at: ValidateRaidItems rejects
// them before it ever gets here, and inventing a 1×1 for one would let an
// unknown id influence a bound instead of being refused.
func carryRects(merged []ItemStack) []carryRect {
	var rects []carryRect
	for _, s := range merged {
		def, ok := ItemDefs[s.ItemID]
		if !ok || s.Quantity <= 0 {
			continue
		}
		stackSize := def.StackSize
		if stackSize < 1 {
			stackSize = 1
		}
		n := (s.Quantity + stackSize - 1) / stackSize
		if s.ItemID == WornBagID && n > 0 {
			n-- // the one on the player's back, which costs no cell
		}
		w, h := def.Width, def.Height
		if w < 1 {
			w = 1
		}
		if h < 1 {
			h = 1
		}
		for i := 0; i < n; i++ {
			rects = append(rects, carryRect{itemID: s.ItemID, w: w, h: h})
		}
	}

	// Largest first, then by shape, then by id — a total order, so the verdict
	// is deterministic for a given haul.
	//
	// The wire order is deliberately not used. MergeStacks has already sorted by
	// item id, which is alphabetical and therefore arbitrary as a packing order,
	// and first fit is order-sensitive: feeding it "chain, bike_frame" could
	// refuse a haul that "bike_frame, chain" holds. Decreasing size is the
	// standard fix and, over this catalog's four shapes (1×1, 2×1, 2×2, 3×2)
	// into these two pages, it never refuses an arrangement another order would
	// have found. The scan itself is the client's, cell for cell.
	sort.Slice(rects, func(i, j int) bool {
		a, b := rects[i], rects[j]
		if a.w*a.h != b.w*b.h {
			return a.w*a.h > b.w*b.h
		}
		if a.h != b.h {
			return a.h > b.h
		}
		if a.w != b.w {
			return a.w > b.w
		}
		return a.itemID < b.itemID
	})
	return rects
}

// FitsCarryGrid reports whether a merged haul could physically have been
// carried: every stack becomes a rectangle and the rectangles must all place
// into CarryPages by the client's own first fit.
//
// This is the check the per-item cap could not make. MaxRaidUnits counts stacks
// and knows nothing about shape, so it allowed thirteen 3×2 bicycle frames into
// a bag whose only 3-wide page holds exactly one — thirteen bicycles' worth of
// tier-1 parts out of a raid that can carry one frame. It is also the only
// aggregate bound there has ever been: before it, thirteen different ids each
// at their own per-item cap passed, against a twelve-cell bag.
//
// Callers must have checked ids and quantities first (ValidateRaidItems does):
// this turns quantities into allocations, so it must never see an unbounded one.
func FitsCarryGrid(merged []ItemStack) error {
	rects := carryRects(merged)
	if len(rects) > TotalCarryCells() {
		// Cheap and exact: every rectangle covers at least one cell.
		return fmt.Errorf("a raid carries at most %d stacks in %d cells, this haul needs %d",
			TotalCarryCells(), TotalCarryCells(), len(rects))
	}

	grid := newOccupancy(CarryPages)
	for _, r := range rects {
		if !grid.firstFit(r.w, r.h) {
			return fmt.Errorf("item %s: this haul does not fit the carry grid — %s is %dx%d cells and the bag is %s",
				r.itemID, r.itemID, r.w, r.h, describePages(CarryPages))
		}
	}
	return nil
}

func describePages(pages []GridPage) string {
	out := ""
	for i, p := range pages {
		if i > 0 {
			out += " + "
		}
		out += fmt.Sprintf("%dx%d", p.Columns, p.Rows)
	}
	return out
}
