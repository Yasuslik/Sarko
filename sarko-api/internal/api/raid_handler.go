package api

import (
	"encoding/json"
	"errors"
	"log/slog"
	"net/http"
	"time"

	"github.com/Yasuslik/sarko-api/internal/auth"
	"github.com/Yasuslik/sarko-api/internal/domain"
	"github.com/Yasuslik/sarko-api/internal/store"
)

// maxSafePocketItems caps what may survive death (§4: one or two items).
const maxSafePocketItems = 2

type startRaidRequest struct {
	MapID   string             `json:"map_id"`
	Loadout []domain.ItemStack `json:"loadout"`
}

type sessionRequest struct {
	SessionID    string `json:"session_id"`
	SessionToken string `json:"session_token"`
}

// confirmResponse tells the client the server's authoritative raid deadline.
type confirmResponse struct {
	ExpiresAt string `json:"expires_at"`
}

type resultRequest struct {
	sessionRequest
	Outcome string             `json:"outcome"`
	Items   []domain.ItemStack `json:"items"`
}

func handleRaidStart(deps Deps) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		playerID, ok := auth.PlayerID(r.Context())
		if !ok {
			WriteError(w, http.StatusUnauthorized, "unauthorized", "no player in context")
			return
		}

		var req startRaidRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			WriteError(w, http.StatusBadRequest, "bad_request", "body must be JSON")
			return
		}
		if req.MapID == "" {
			WriteError(w, http.StatusBadRequest, "bad_request", "map_id is required")
			return
		}
		if err := domain.ValidateStacks(req.Loadout); err != nil {
			WriteError(w, http.StatusBadRequest, "bad_request", err.Error())
			return
		}

		started, err := deps.Store.StartRaid(r.Context(), store.StartRaidParams{
			PlayerID:   playerID,
			MapID:      req.MapID,
			Loadout:    req.Loadout,
			PendingTTL: deps.PendingTTL,
		})
		switch {
		case errors.Is(err, store.ErrMapLocked):
			WriteError(w, http.StatusForbidden, "map_locked", "your garage does not unlock this map")
		case errors.Is(err, store.ErrRaidInProgress):
			WriteError(w, http.StatusConflict, "raid_in_progress", "finish the current raid first")
		case errors.Is(err, store.ErrInsufficientItems):
			WriteError(w, http.StatusConflict, "insufficient_items", err.Error())
		case errors.Is(err, store.ErrNotFound):
			WriteError(w, http.StatusNotFound, "not_found", "player does not exist")
		case err != nil:
			slog.Error("start raid", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not start raid")
		default:
			WriteJSON(w, http.StatusOK, started)
		}
	}
}

func handleRaidConfirm(deps Deps) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req sessionRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			WriteError(w, http.StatusBadRequest, "bad_request", "body must be JSON")
			return
		}
		if req.SessionID == "" || req.SessionToken == "" {
			WriteError(w, http.StatusBadRequest, "bad_request", "session_id and session_token are required")
			return
		}

		// The store records one already-computed deadline: the raid duration
		// plus the grace buffer that covers a slow result submission.
		expiresAt, err := deps.Store.ConfirmRaid(r.Context(),
			req.SessionID, req.SessionToken, deps.RaidTTL+deps.GraceBuffer)
		switch {
		case errors.Is(err, store.ErrSessionNotOpen):
			WriteError(w, http.StatusConflict, "session_not_open", "session is not pending")
		case err != nil:
			slog.Error("confirm raid", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not confirm raid")
		default:
			// The deadline is echoed back so the client aligns its in-raid
			// timer to the server rather than guessing it.
			WriteJSON(w, http.StatusOK, confirmResponse{ExpiresAt: expiresAt.UTC().Format(time.RFC3339)})
		}
	}
}

func handleRaidResult(deps Deps) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req resultRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			WriteError(w, http.StatusBadRequest, "bad_request", "body must be JSON")
			return
		}
		if req.SessionID == "" || req.SessionToken == "" {
			WriteError(w, http.StatusBadRequest, "bad_request", "session_id and session_token are required")
			return
		}
		if !domain.IsValidOutcome(req.Outcome) {
			WriteError(w, http.StatusBadRequest, "bad_request", "outcome must be extracted or died")
			return
		}
		if err := domain.ValidateStacks(req.Items); err != nil {
			WriteError(w, http.StatusBadRequest, "bad_request", err.Error())
			return
		}
		if domain.RaidOutcome(req.Outcome) == domain.OutcomeDied &&
			len(domain.MergeStacks(req.Items)) > maxSafePocketItems {
			WriteError(w, http.StatusBadRequest, "safe_pocket_overflow",
				"death may preserve at most two item stacks")
			return
		}

		result, err := deps.Store.SubmitResult(r.Context(), store.SubmitResultParams{
			SessionID:    req.SessionID,
			SessionToken: req.SessionToken,
			Outcome:      domain.RaidOutcome(req.Outcome),
			Items:        req.Items,
		})
		switch {
		case errors.Is(err, store.ErrBadSessionToken):
			WriteError(w, http.StatusUnauthorized, "bad_session_token", "session token does not match")
		case errors.Is(err, store.ErrSessionNotOpen):
			WriteError(w, http.StatusConflict, "session_not_open", "session cannot accept a result")
		case errors.Is(err, store.ErrNotFound):
			WriteError(w, http.StatusNotFound, "not_found", "session does not exist")
		case err != nil:
			slog.Error("submit result", "err", err)
			WriteError(w, http.StatusInternalServerError, "internal", "could not record result")
		default:
			WriteJSON(w, http.StatusOK, result)
		}
	}
}
