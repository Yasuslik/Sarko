package api

import (
	"fmt"
	"log/slog"
	"net/http"
)

// maxDeviceIDLen bounds the only string an unauthenticated caller can write
// into the database. device_id is a client-generated identifier (a UUID or a
// vendor id), so 128 characters is generous; without a cap, /v1/auth/anonymous
// lets anyone store 64 KB per row and create rows at will.
const maxDeviceIDLen = 128

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
		if !decodeJSON(w, r, &req) {
			return
		}
		if req.DeviceID == "" {
			WriteError(w, http.StatusBadRequest, "bad_request", "device_id is required")
			return
		}
		if len(req.DeviceID) > maxDeviceIDLen {
			WriteError(w, http.StatusBadRequest, "bad_request",
				fmt.Sprintf("device_id must be at most %d characters", maxDeviceIDLen))
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
