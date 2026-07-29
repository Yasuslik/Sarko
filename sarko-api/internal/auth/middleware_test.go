package auth

import (
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func protectedHandler(t *testing.T, wantPlayer string) http.Handler {
	t.Helper()
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		id, ok := PlayerID(r.Context())
		if !ok {
			t.Error("PlayerID missing from context inside a protected handler")
		}
		if id != wantPlayer {
			t.Errorf("player id = %q, want %q", id, wantPlayer)
		}
		w.WriteHeader(http.StatusOK)
	})
}

func TestMiddlewarePassesValidToken(t *testing.T) {
	iss := Issuer{Secret: []byte("s"), TTL: time.Hour}
	token, err := iss.Issue("player-9")
	if err != nil {
		t.Fatalf("Issue: %v", err)
	}

	req := httptest.NewRequest(http.MethodGet, "/x", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	rec := httptest.NewRecorder()

	Middleware(iss)(protectedHandler(t, "player-9")).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Errorf("status = %d, want 200", rec.Code)
	}
}

func TestMiddlewareRejectsBadAuth(t *testing.T) {
	iss := Issuer{Secret: []byte("s"), TTL: time.Hour}
	deny := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		t.Error("handler must not run for an unauthenticated request")
	})

	cases := map[string]string{
		"missing header": "",
		"wrong scheme":   "Basic abc",
		"garbage token":  "Bearer not-a-jwt",
	}
	for name, header := range cases {
		t.Run(name, func(t *testing.T) {
			req := httptest.NewRequest(http.MethodGet, "/x", nil)
			if header != "" {
				req.Header.Set("Authorization", header)
			}
			rec := httptest.NewRecorder()

			Middleware(iss)(deny).ServeHTTP(rec, req)

			if rec.Code != http.StatusUnauthorized {
				t.Errorf("status = %d, want 401", rec.Code)
			}
		})
	}
}
