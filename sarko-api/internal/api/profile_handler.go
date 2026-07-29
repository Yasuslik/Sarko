package api

import (
	"errors"
	"log/slog"
	"net/http"

	"github.com/Yasuslik/sarko-api/internal/auth"
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
