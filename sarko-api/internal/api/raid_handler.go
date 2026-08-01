package api

import (
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
		if !decodeJSON(w, r, &req) {
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
		// The loadout is content-checked too: an id the game does not define
		// cannot have been in a stash that only this API ever writes to, so a
		// loadout naming one is either a stale client or a forged request.
		if err := domain.ValidateRaidItems(req.Loadout); err != nil {
			WriteError(w, http.StatusBadRequest, "implausible_items", err.Error())
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
		playerID, ok := auth.PlayerID(r.Context())
		if !ok {
			WriteError(w, http.StatusUnauthorized, "unauthorized", "no player in context")
			return
		}

		var req sessionRequest
		if !decodeJSON(w, r, &req) {
			return
		}
		if req.SessionID == "" || req.SessionToken == "" {
			WriteError(w, http.StatusBadRequest, "bad_request", "session_id and session_token are required")
			return
		}

		// The store records one already-computed deadline: the raid duration
		// plus the grace buffer that covers a slow result submission.
		expiresAt, err := deps.Store.ConfirmRaid(r.Context(),
			playerID, req.SessionID, req.SessionToken, deps.RaidTTL+deps.GraceBuffer)
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
		playerID, ok := auth.PlayerID(r.Context())
		if !ok {
			WriteError(w, http.StatusUnauthorized, "unauthorized", "no player in context")
			return
		}

		var req resultRequest
		if !decodeJSON(w, r, &req) {
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
		// Spec §4.7: the UE server is a client to this API and is not trusted
		// with arbitrary item grants. This is the count-and-quantity floor —
		// every id must exist, the haul must fit the 12-slot backpack, and no
		// stack may exceed what a backpack holds.
		if err := domain.ValidateRaidItems(req.Items); err != nil {
			WriteError(w, http.StatusBadRequest, "implausible_items", err.Error())
			return
		}
		if domain.RaidOutcome(req.Outcome) == domain.OutcomeDied &&
			len(domain.MergeStacks(req.Items)) > maxSafePocketItems {
			WriteError(w, http.StatusBadRequest, "safe_pocket_overflow",
				"death may preserve at most two item stacks")
			return
		}

		result, err := deps.Store.SubmitResult(r.Context(), store.SubmitResultParams{
			PlayerID:     playerID,
			SessionID:    req.SessionID,
			SessionToken: req.SessionToken,
			Outcome:      domain.RaidOutcome(req.Outcome),
			Items:        req.Items,
		})
		switch {
		case errors.Is(err, store.ErrBadSessionToken):
			WriteError(w, http.StatusUnauthorized, "bad_session_token", "session token does not match")
		case errors.Is(err, store.ErrSessionNotConfirmed):
			// Its own code, not session_not_open: the session is fine, the
			// caller skipped /v1/raid/confirm. A client that gets this has a
			// bug it can act on; a forged one is being told nothing it did not
			// already know from having made the call.
			WriteError(w, http.StatusConflict, "session_not_confirmed",
				"confirm the raid before submitting a result")
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
