package api

import (
	"errors"
	"log/slog"
	"net/http"

	"github.com/Yasuslik/sarko-api/internal/auth"
	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
)

func handleProfile(deps Deps) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		playerID, ok := auth.PlayerID(r.Context())
		if !ok {
			WriteError(w, http.StatusUnauthorized, "unauthorized", "no player in context")
			return
		}

		profile, err := deps.Store.Profile(r.Context(), playerID)
		if errors.Is(err, store.ErrNotFound) {
			WriteError(w, http.StatusNotFound, "not_found", "player does not exist")
			return
		}
		if err != nil {
			slog.Error("read profile", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not read profile")
			return
		}
		WriteJSON(w, http.StatusOK, profile)
	}
}

// setEquipmentRequest names one slot and what goes in it. An EMPTY item_id is
// not an omission, it is the unequip: "put nothing in this slot".
type setEquipmentRequest struct {
	Slot   string `json:"slot"`
	ItemID string `json:"item_id"`
}

// equipmentResponse is the equipment as it stands after the write, so the client
// never has to guess what it now looks like — and so an optimistic client-side
// update is corrected by the answer rather than by the next profile fetch.
type equipmentResponse struct {
	Equipment map[string]string `json:"equipment"`
}

// handleSetEquipment is POST /v1/profile/equipment.
//
// One slot per call, not the whole set. A whole-set write would make "equip this"
// and "unequip that" the same request, so a client with a stale idea of one slot
// would silently clear another — and the tap this endpoint serves is always about
// exactly one slot (equipment spec §2: "equipping is a tap").
//
// It is on the profile and not under /v1/raid because it is player state that
// outlives any raid, which is the whole reason it is stored here at all (§6).
func handleSetEquipment(deps Deps) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		playerID, ok := auth.PlayerID(r.Context())
		if !ok {
			WriteError(w, http.StatusUnauthorized, "unauthorized", "no player in context")
			return
		}

		var req setEquipmentRequest
		if !decodeJSON(w, r, &req) {
			return
		}
		// The slot is checked here as well as in the store, because a bad slot is a
		// 400 (the caller's request is malformed) while a wrong item for a good slot
		// is a 409 (the request is well-formed and refused). Collapsing the two
		// would tell a client with a typo to go and find a different item.
		if !domain.IsValidEquipSlot(req.Slot) {
			WriteError(w, http.StatusBadRequest, "bad_request",
				"slot must be weapon, backpack or clothing")
			return
		}
		// Bounded before it reaches a query, the same way ValidateStacks bounds an
		// item id, and never echoed back.
		if len(req.ItemID) > domain.MaxItemIDLen {
			WriteError(w, http.StatusBadRequest, "bad_request", "item_id is too long")
			return
		}

		equipment, err := deps.Store.SetEquipment(r.Context(), playerID,
			domain.EquipSlot(req.Slot), req.ItemID)
		switch {
		case errors.Is(err, store.ErrNotEquippable):
			// 409 and not 400: the request is well-formed and the server is refusing
			// it on the game's rules, which is the same shape /v1/garage/craft's
			// insufficient_items has. The message is the reason, and the client shows
			// it verbatim — a refused equip with no reason is exactly what the
			// container panel's refusal discipline exists to prevent.
			WriteError(w, http.StatusConflict, "not_equippable", err.Error())
		case errors.Is(err, store.ErrInsufficientItems):
			WriteError(w, http.StatusConflict, "insufficient_items", err.Error())
		case errors.Is(err, store.ErrNotFound):
			WriteError(w, http.StatusNotFound, "not_found", "player does not exist")
		case err != nil:
			slog.Error("set equipment", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not set equipment")
		default:
			WriteJSON(w, http.StatusOK, equipmentResponse{Equipment: equipment})
		}
	}
}
