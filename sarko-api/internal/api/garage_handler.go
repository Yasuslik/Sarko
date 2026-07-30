package api

import (
	"errors"
	"log/slog"
	"net/http"

	"github.com/Yasuslik/sarko-api/internal/auth"
	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
)

type craftResponse struct {
	VehicleTier  domain.Tier `json:"vehicle_tier"`
	UnlockedMaps []string    `json:"unlocked_maps"`
}

func handleGarageCraft(deps Deps) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		playerID, ok := auth.PlayerID(r.Context())
		if !ok {
			WriteError(w, http.StatusUnauthorized, "unauthorized", "no player in context")
			return
		}

		tier, err := deps.Store.CraftNextVehicle(r.Context(), playerID)
		switch {
		case errors.Is(err, store.ErrInsufficientItems):
			WriteError(w, http.StatusConflict, "insufficient_items", err.Error())
		case errors.Is(err, store.ErrMaxTier):
			WriteError(w, http.StatusConflict, "max_tier", "every vehicle is already built")
		case errors.Is(err, store.ErrNotFound):
			WriteError(w, http.StatusNotFound, "not_found", "player does not exist")
		case err != nil:
			slog.Error("craft vehicle", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not craft vehicle")
		default:
			WriteJSON(w, http.StatusOK, craftResponse{
				VehicleTier:  tier,
				UnlockedMaps: domain.UnlockedMaps(tier),
			})
		}
	}
}
