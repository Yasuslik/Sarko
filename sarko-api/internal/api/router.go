package api

import (
	"net/http"
	"time"

	"github.com/Yasuslik/sarko-api/internal/auth"
	"github.com/Yasuslik/sarko-api/internal/store"
)

// Deps carries everything the handlers need.
type Deps struct {
	Store      *store.Store
	Issuer     auth.Issuer
	RaidTTL    time.Duration
	PendingTTL time.Duration
	// GraceBuffer is added to RaidTTL to form the confirmed raid's server-side
	// deadline. It exists so a result submitted just inside the client's own
	// timer is still credited when the network delays it.
	GraceBuffer time.Duration
}

// NewRouter builds the HTTP route table.
func NewRouter(deps Deps) http.Handler {
	mux := http.NewServeMux()

	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		WriteJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	})

	// Unauthenticated: this is where a device gets its token.
	mux.Handle("POST /v1/auth/anonymous", handleAnonymousAuth(deps))

	// Authenticated endpoints.
	protected := auth.Middleware(deps.Issuer)
	mux.Handle("GET /v1/profile", protected(handleProfile(deps)))
	mux.Handle("POST /v1/raid/start", protected(handleRaidStart(deps)))
	mux.Handle("POST /v1/raid/confirm", protected(handleRaidConfirm(deps)))
	mux.Handle("POST /v1/raid/result", protected(handleRaidResult(deps)))
	mux.Handle("POST /v1/garage/craft", protected(handleGarageCraft(deps)))

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		WriteError(w, http.StatusNotFound, "not_found", "no such endpoint")
	})

	return mux
}
