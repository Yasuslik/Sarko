package api

import (
	"encoding/json"
	"log/slog"
	"net/http"
)

type anonymousRequest struct {
	DeviceID string `json:"device_id"`
}

type anonymousResponse struct {
	PlayerID string `json:"player_id"`
	Token    string `json:"token"`
}

func handleAnonymousAuth(deps Deps) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req anonymousRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			WriteError(w, http.StatusBadRequest, "bad_request", "body must be JSON")
			return
		}
		if req.DeviceID == "" {
			WriteError(w, http.StatusBadRequest, "bad_request", "device_id is required")
			return
		}

		playerID, err := deps.Store.UpsertPlayer(r.Context(), req.DeviceID)
		if err != nil {
			slog.Error("upsert player", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not create player")
			return
		}
		token, err := deps.Issuer.Issue(playerID)
		if err != nil {
			slog.Error("issue token", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not issue token")
			return
		}
		WriteJSON(w, http.StatusOK, anonymousResponse{PlayerID: playerID, Token: token})
	}
}
