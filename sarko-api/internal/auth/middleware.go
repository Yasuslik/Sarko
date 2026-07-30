package auth

import (
	"context"
	"net/http"
	"strings"
)

type contextKey struct{}

var playerKey contextKey

// Middleware rejects requests without a valid Bearer token and puts the
// player id in the request context for downstream handlers.
func Middleware(iss Issuer) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			header := r.Header.Get("Authorization")
			token, ok := bearer(header)
			if !ok {
				unauthorized(w)
				return
			}
			playerID, err := iss.Verify(token)
			if err != nil {
				unauthorized(w)
				return
			}
			ctx := context.WithValue(r.Context(), playerKey, playerID)
			next.ServeHTTP(w, r.WithContext(ctx))
		})
	}
}

// PlayerID reads the authenticated player id placed by Middleware.
func PlayerID(ctx context.Context) (string, bool) {
	id, ok := ctx.Value(playerKey).(string)
	return id, ok
}

func bearer(header string) (string, bool) {
	const prefix = "Bearer "
	if len(header) <= len(prefix) || !strings.EqualFold(header[:len(prefix)], prefix) {
		return "", false
	}
	return strings.TrimSpace(header[len(prefix):]), true
}

func unauthorized(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusUnauthorized)
	_, _ = w.Write([]byte(`{"error":{"code":"unauthorized","message":"valid Bearer token required"}}`))
}
