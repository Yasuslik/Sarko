package api_test

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/Yasuslik/sarko-api/internal/api"
	"github.com/Yasuslik/sarko-api/internal/auth"
	"github.com/Yasuslik/sarko-api/internal/store"
	"github.com/Yasuslik/sarko-api/internal/testutil"
)

type client struct {
	t     *testing.T
	base  string
	token string
}

func newTestServer(t *testing.T) *client {
	t.Helper()
	deps := api.Deps{
		Store:      store.New(testutil.Pool(t)),
		Issuer:     auth.Issuer{Secret: []byte("test-secret"), TTL: time.Hour},
		RaidTTL:    time.Minute,
		PendingTTL: time.Minute,
	}
	srv := httptest.NewServer(api.NewRouter(deps))
	t.Cleanup(srv.Close)
	return &client{t: t, base: srv.URL}
}

func (c *client) do(method, path string, body any, out any) int {
	c.t.Helper()

	var reader *bytes.Reader
	if body != nil {
		raw, err := json.Marshal(body)
		if err != nil {
			c.t.Fatalf("marshal request: %v", err)
		}
		reader = bytes.NewReader(raw)
	} else {
		reader = bytes.NewReader(nil)
	}

	req, err := http.NewRequest(method, c.base+path, reader)
	if err != nil {
		c.t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	if c.token != "" {
		req.Header.Set("Authorization", "Bearer "+c.token)
	}

	res, err := http.DefaultClient.Do(req)
	if err != nil {
		c.t.Fatalf("%s %s: %v", method, path, err)
	}
	defer res.Body.Close()

	if out != nil {
		if err := json.NewDecoder(res.Body).Decode(out); err != nil {
			c.t.Fatalf("decode %s %s: %v", method, path, err)
		}
	}
	return res.StatusCode
}

func (c *client) login(device string) {
	c.t.Helper()
	var out struct {
		Token string `json:"token"`
	}
	if code := c.do(http.MethodPost, "/v1/auth/anonymous",
		map[string]string{"device_id": device}, &out); code != http.StatusOK {
		c.t.Fatalf("auth status = %d, want 200", code)
	}
	if out.Token == "" {
		c.t.Fatal("auth returned an empty token")
	}
	c.token = out.Token
}

func TestFullRaidCycleOverHTTP(t *testing.T) {
	c := newTestServer(t)
	c.login("device-e2e")

	var profile store.Profile
	if code := c.do(http.MethodGet, "/v1/profile", nil, &profile); code != http.StatusOK {
		t.Fatalf("profile status = %d, want 200", code)
	}
	if len(profile.UnlockedMaps) == 0 {
		t.Fatal("a new player must have at least one unlocked map")
	}

	var started store.StartedRaid
	code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "forest", "loadout": []any{}}, &started)
	if code != http.StatusOK {
		t.Fatalf("raid/start status = %d, want 200", code)
	}

	if code := c.do(http.MethodPost, "/v1/raid/confirm", map[string]string{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
	}, nil); code != http.StatusNoContent {
		t.Fatalf("raid/confirm status = %d, want 204", code)
	}

	var result store.RaidResult
	code = c.do(http.MethodPost, "/v1/raid/result", map[string]any{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
		"outcome":       "extracted",
		"items":         []map[string]any{{"item_id": "chain", "quantity": 1}},
	}, &result)
	if code != http.StatusOK {
		t.Fatalf("raid/result status = %d, want 200", code)
	}

	if code := c.do(http.MethodGet, "/v1/profile", nil, &profile); code != http.StatusOK {
		t.Fatalf("profile status = %d, want 200", code)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].ItemID != "chain" {
		t.Errorf("stash = %v, want one chain", profile.Stash)
	}
}

func TestProtectedEndpointsRequireAuth(t *testing.T) {
	c := newTestServer(t)

	tests := []struct {
		method string
		path   string
		body   any
	}{
		{http.MethodGet, "/v1/profile", nil},
		{http.MethodPost, "/v1/raid/start", map[string]any{"map_id": "forest"}},
		{http.MethodPost, "/v1/raid/confirm", map[string]string{
			"session_id":    "dummy",
			"session_token": "dummy",
		}},
		{http.MethodPost, "/v1/raid/result", map[string]any{
			"session_id":    "dummy",
			"session_token": "dummy",
			"outcome":       "extracted",
			"items":         []map[string]any{},
		}},
		{http.MethodPost, "/v1/garage/craft", map[string]any{}},
	}

	for _, tt := range tests {
		code := c.do(tt.method, tt.path, tt.body, nil)
		if code != http.StatusUnauthorized {
			t.Errorf("%s %s without token = %d, want 401", tt.method, tt.path, code)
		}
	}
}

func TestRaidStartRejectsLockedMapWith403(t *testing.T) {
	c := newTestServer(t)
	c.login("device-locked-http")

	code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "airbase", "loadout": []any{}}, nil)
	if code != http.StatusForbidden {
		t.Errorf("locked map status = %d, want 403", code)
	}
}

func TestDeathResultRejectsOversizedSafePocket(t *testing.T) {
	c := newTestServer(t)
	c.login("device-cheater")

	var started store.StartedRaid
	if code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "forest", "loadout": []any{}}, &started); code != http.StatusOK {
		t.Fatalf("raid/start status = %d, want 200", code)
	}
	if code := c.do(http.MethodPost, "/v1/raid/confirm", map[string]string{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
	}, nil); code != http.StatusNoContent {
		t.Fatalf("raid/confirm status = %d, want 204", code)
	}

	code := c.do(http.MethodPost, "/v1/raid/result", map[string]any{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
		"outcome":       "died",
		"items": []map[string]any{
			{"item_id": "turbine", "quantity": 1},
			{"item_id": "gearbox", "quantity": 1},
			{"item_id": "battery", "quantity": 1},
		},
	}, nil)
	if code != http.StatusBadRequest {
		t.Errorf("three safe-pocket items on death = %d, want 400", code)
	}
}

func TestDeathResultAllowsSplitEntriesOfOneItem(t *testing.T) {
	c := newTestServer(t)
	c.login("device-splitter")

	var started store.StartedRaid
	if code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "forest", "loadout": []any{}}, &started); code != http.StatusOK {
		t.Fatalf("raid/start status = %d, want 200", code)
	}
	if code := c.do(http.MethodPost, "/v1/raid/confirm", map[string]string{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
	}, nil); code != http.StatusNoContent {
		t.Fatalf("raid/confirm status = %d, want 204", code)
	}

	// Build 50 entries of the same item id: they merge to one stack, within cap of 2.
	items := make([]map[string]any, 50)
	for i := 0; i < 50; i++ {
		items[i] = map[string]any{"item_id": "chain", "quantity": 1}
	}

	var result store.RaidResult
	code := c.do(http.MethodPost, "/v1/raid/result", map[string]any{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
		"outcome":       "died",
		"items":         items,
	}, &result)
	if code != http.StatusOK {
		t.Fatalf("raid/result with 50 split entries of one item = %d, want 200", code)
	}

	// Verify quantities were summed into the stash.
	var profile store.Profile
	if code := c.do(http.MethodGet, "/v1/profile", nil, &profile); code != http.StatusOK {
		t.Fatalf("profile status = %d, want 200", code)
	}
	if len(profile.Stash) != 1 || profile.Stash[0].ItemID != "chain" || profile.Stash[0].Quantity != 50 {
		t.Errorf("stash = %v, want one chain with quantity 50", profile.Stash)
	}
}
