package api_test

import (
	"net/http"
	"testing"
	"time"

	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
)

// ВИЛАЗКА at the wire (spec §4.5), which is where the trust boundary actually is:
// a client sends `mode` and NOTHING else about the free run, and every consequence
// — free entry, the kit, the cooldown, the shorter clock — comes back decided.
func TestSortieOverHTTP(t *testing.T) {
	c := newTestServer(t)
	c.login("device-sortie")

	// Spend the starter kit's pistol so this player is genuinely broke, which is the
	// state the mechanic exists for. Equip it, take it into a raid, and die.
	var equipped struct {
		Equipment map[string]string `json:"equipment"`
	}
	if code := c.do(http.MethodPost, "/v1/profile/equipment",
		map[string]string{"slot": "weapon", "item_id": "pistol"}, &equipped); code != http.StatusOK {
		t.Fatalf("equip status = %d, want 200", code)
	}
	var lost store.StartedRaid
	if code := c.do(http.MethodPost, "/v1/raid/start", map[string]any{
		"map_id":  "bridge",
		"loadout": domain.EquipmentLoadout(equipped.Equipment),
	}, &lost); code != http.StatusOK {
		t.Fatalf("raid/start status = %d, want 200", code)
	}
	c.confirm(lost)
	var deadResult store.RaidResult
	if code := c.do(http.MethodPost, "/v1/raid/result", map[string]any{
		"session_id": lost.SessionID, "session_token": lost.SessionToken,
		"outcome": "died", "items": []any{},
	}, &deadResult); code != http.StatusOK {
		t.Fatalf("raid/result status = %d, want 200", code)
	}

	var profile store.Profile
	if code := c.do(http.MethodGet, "/v1/profile", nil, &profile); code != http.StatusOK {
		t.Fatalf("profile status = %d, want 200", code)
	}
	for _, stack := range profile.Stash {
		if stack.ItemID == "pistol" {
			t.Fatal("the pistol should have died with the raid — this test needs a broke player")
		}
	}
	if profile.SortieCooldownSeconds != 0 {
		t.Errorf("sortie_cooldown_seconds = %d, want 0 for a player who has never taken one",
			profile.SortieCooldownSeconds)
	}

	// THE SORTIE. The body says `sortie` and nothing else: no kit, no cooldown claim.
	// The loadout it does send is deliberately a lie — a pistol this player has just
	// lost — because a sortie must discard it unread rather than refuse the run.
	var sortie store.StartedRaid
	code := c.do(http.MethodPost, "/v1/raid/start", map[string]any{
		"map_id":  "bridge",
		"mode":    "sortie",
		"loadout": []map[string]any{{"item_id": "pistol", "quantity": 1}},
	}, &sortie)
	if code != http.StatusOK {
		t.Fatalf("sortie start status = %d, want 200 — a broke player must always be able to take one", code)
	}
	if sortie.Mode != domain.ModeSortie {
		t.Errorf("mode = %q, want sortie", sortie.Mode)
	}
	if len(sortie.GrantedKit) == 0 {
		t.Fatal("the response must carry the granted kit, or the client cannot show what it was lent")
	}

	// The shorter clock. The sortie's deadline is SortieTTL + GraceBuffer (50 s at
	// test scale) against a raid's RaidTTL + GraceBuffer (90 s) — so a sortie that
	// came back with the raid's deadline would land past this bound.
	expiresAt := c.confirm(sortie)
	if left := time.Until(expiresAt); left > 70*time.Second {
		t.Errorf("the sortie's deadline is %s away, want the shorter sortie clock", left)
	}

	// Extraction credits the borrowed gear AND the haul.
	var result store.RaidResult
	code = c.do(http.MethodPost, "/v1/raid/result", map[string]any{
		"session_id": sortie.SessionID, "session_token": sortie.SessionToken,
		"outcome": "extracted",
		"items":   []map[string]any{{"item_id": "scrap_metal", "quantity": 2}},
	}, &result)
	if code != http.StatusOK {
		t.Fatalf("sortie result status = %d, want 200", code)
	}
	if result.Mode != domain.ModeSortie {
		t.Errorf("result mode = %q, want sortie", result.Mode)
	}

	if code := c.do(http.MethodGet, "/v1/profile", nil, &profile); code != http.StatusOK {
		t.Fatalf("profile status = %d, want 200", code)
	}
	held := map[string]int{}
	for _, stack := range profile.Stash {
		held[stack.ItemID] = stack.Quantity
	}
	for _, stack := range sortie.GrantedKit {
		if held[stack.ItemID] < stack.Quantity {
			t.Errorf("stash holds %d %s, the sortie lent %d — what you extract is yours",
				held[stack.ItemID], stack.ItemID, stack.Quantity)
		}
	}
	if held["scrap_metal"] != 2 {
		t.Errorf("the haul must credit as well, stash holds %d scrap", held["scrap_metal"])
	}

	// It does not count as the tutorial, even though it extracted.
	if profile.TutorialCompleted {
		t.Error("a sortie extraction must not latch tutorial_completed")
	}

	// The cooldown is now on, and the client is told how long — this is the number the
	// second button draws.
	if profile.SortieCooldownSeconds <= 0 {
		t.Error("after a sortie the profile must report a positive cooldown, or the button has nothing to show")
	}

	// And the request made during it is REFUSED BY NAME.
	var refusal struct {
		Error struct {
			Code    string `json:"code"`
			Message string `json:"message"`
		} `json:"error"`
	}
	code = c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "bridge", "mode": "sortie"}, &refusal)
	if code != http.StatusConflict || refusal.Error.Code != "sortie_cooldown" {
		t.Fatalf("a sortie during the cooldown: status %d code %q, want 409 sortie_cooldown",
			code, refusal.Error.Code)
	}
	if refusal.Error.Message == "" {
		t.Error("the refusal must carry a reason — the client shows it verbatim")
	}

	// An ORDINARY raid is still allowed, because the cooldown is on the free run and
	// not on playing. The player is armed now, so this is a real one.
	var normal store.StartedRaid
	if code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "bridge", "loadout": []any{}}, &normal); code != http.StatusOK {
		t.Fatalf("the sortie cooldown must not block a raid: status %d", code)
	}
}

// A mode this service does not have is the CALLER's mistake and is told so, rather
// than being silently downgraded to a raid — a client that asked for something and
// got something else quietly is a client that will debit a stash it meant to keep.
func TestUnknownRaidModeIs400(t *testing.T) {
	c := newTestServer(t)
	c.login("device-bad-mode")

	var refusal struct {
		Error struct {
			Code string `json:"code"`
		} `json:"error"`
	}
	code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "bridge", "mode": "free"}, &refusal)
	if code != http.StatusBadRequest || refusal.Error.Code != "bad_request" {
		t.Fatalf("mode=free: status %d code %q, want 400 bad_request", code, refusal.Error.Code)
	}

	// An ABSENT mode is a raid, which is what every client built before this field
	// existed sends. Defaulting to the mode that costs the player something is the
	// only safe direction.
	var started store.StartedRaid
	if code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "bridge"}, &started); code != http.StatusOK {
		t.Fatalf("an omitted mode must still start a raid: status %d", code)
	}
	if started.Mode != domain.ModeRaid {
		t.Errorf("mode = %q, want raid", started.Mode)
	}
	if started.GrantedKit != nil {
		t.Errorf("an ordinary raid must grant no kit, got %v", started.GrantedKit)
	}
}
