package api_test

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
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
		Store:       store.New(testutil.Pool(t)),
		Issuer:      auth.Issuer{Secret: []byte("test-secret"), TTL: time.Hour},
		RaidTTL:     time.Minute,
		PendingTTL:  time.Minute,
		GraceBuffer: 30 * time.Second,
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

// confirm calls POST /v1/raid/confirm and asserts the contract the client
// depends on: 200 with the server's authoritative deadline as RFC 3339, not a
// bare 204 that would leave the client guessing when the raid really ends.
func (c *client) confirm(started store.StartedRaid) time.Time {
	c.t.Helper()
	var out struct {
		ExpiresAt string `json:"expires_at"`
	}
	code := c.do(http.MethodPost, "/v1/raid/confirm", map[string]string{
		"session_id":    started.SessionID,
		"session_token": started.SessionToken,
	}, &out)
	if code != http.StatusOK {
		c.t.Fatalf("raid/confirm status = %d, want 200", code)
	}
	expiresAt, err := time.Parse(time.RFC3339, out.ExpiresAt)
	if err != nil {
		c.t.Fatalf("raid/confirm expires_at = %q, want RFC 3339: %v", out.ExpiresAt, err)
	}
	return expiresAt
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

	c.confirm(started)

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

// TestConfirmReturnsDeadlineIncludingGraceBuffer pins the fix for the bug where
// a late-but-legitimate extraction was recorded as death: the server's deadline
// is the raid duration *plus* a grace buffer, and confirm reports it so the
// client can align its own timer instead of guessing. A bare 204 (the old
// behaviour) gives the client nothing to align to.
func TestConfirmReturnsDeadlineIncludingGraceBuffer(t *testing.T) {
	c := newTestServer(t)
	c.login("device-grace")

	var started store.StartedRaid
	if code := c.do(http.MethodPost, "/v1/raid/start",
		map[string]any{"map_id": "forest", "loadout": []any{}}, &started); code != http.StatusOK {
		t.Fatalf("raid/start status = %d, want 200", code)
	}

	before := time.Now()
	expiresAt := c.confirm(started)

	// newTestServer uses RaidTTL 1m + GraceBuffer 30s.
	want := before.Add(90 * time.Second)
	if diff := expiresAt.Sub(want); diff < -5*time.Second || diff > 5*time.Second {
		t.Errorf("expires_at = %v, want ~%v (RaidTTL + GraceBuffer)", expiresAt, want)
	}
	// The buffer must actually be present: strictly later than RaidTTL alone.
	if !expiresAt.After(before.Add(time.Minute)) {
		t.Errorf("expires_at = %v is not past RaidTTL alone — the grace buffer is missing", expiresAt)
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

// postRaw sends an arbitrary byte body, bypassing json.Marshal, so a body
// larger than the reader's cap can actually be produced. It returns the status
// and the decoded error envelope.
func (c *client) postRaw(path string, body []byte) (int, errorEnvelope) {
	c.t.Helper()

	req, err := http.NewRequest(http.MethodPost, c.base+path, bytes.NewReader(body))
	if err != nil {
		c.t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	if c.token != "" {
		req.Header.Set("Authorization", "Bearer "+c.token)
	}

	res, err := http.DefaultClient.Do(req)
	if err != nil {
		c.t.Fatalf("POST %s: %v", path, err)
	}
	defer res.Body.Close()

	var env errorEnvelope
	if err := json.NewDecoder(res.Body).Decode(&env); err != nil {
		c.t.Fatalf("decode error envelope from POST %s: %v", path, err)
	}
	return res.StatusCode, env
}

type errorEnvelope struct {
	Error struct {
		Code    string `json:"code"`
		Message string `json:"message"`
	} `json:"error"`
}

// TestOversizedBodyIsRejectedAsBadRequest covers every JSON-decoding endpoint,
// including the unauthenticated one. Without http.MaxBytesReader the decoder
// would happily allocate whatever an anonymous caller sends; with it, but
// without the errors.As branch, the failure would surface as a 500.
//
// The message assertion is the load-bearing part. Every one of these endpoints
// returns 400 for an unrecognised body anyway (missing device_id, missing
// session_id), so a status-only check would stay green with the cap removed —
// only the distinct "body is too large" message proves the read was truncated
// instead of fully buffered.
func TestOversizedBodyIsRejectedAsBadRequest(t *testing.T) {
	c := newTestServer(t)

	// One JSON string field, well past the 64 KB reader cap.
	huge := append([]byte(`{"device_id":"`), bytes.Repeat([]byte("a"), 200<<10)...)
	huge = append(huge, []byte(`"}`)...)

	paths := []string{
		"/v1/auth/anonymous", // unauthenticated: the exposed surface
		"/v1/raid/start",
		"/v1/raid/confirm",
		"/v1/raid/result",
	}

	// The unauthenticated endpoint is checked without a token; the protected
	// ones need one, or they would 401 before the body is ever read.
	for _, path := range paths {
		if path != "/v1/auth/anonymous" && c.token == "" {
			c.login("device-oversized")
		}
		code, env := c.postRaw(path, huge)
		if code != http.StatusBadRequest {
			t.Errorf("POST %s with a %d-byte body = %d, want 400", path, len(huge), code)
		}
		if env.Error.Code != "bad_request" {
			t.Errorf("POST %s error code = %q, want bad_request", path, env.Error.Code)
		}
		if env.Error.Message != "body is too large" {
			t.Errorf("POST %s message = %q, want %q — the body was not capped",
				path, env.Error.Message, "body is too large")
		}
	}
}

// TestBodyUnderTheCapIsStillAccepted keeps the cap from being tightened into
// something that rejects legitimate traffic: a large-but-legal body decodes.
func TestBodyUnderTheCapIsStillAccepted(t *testing.T) {
	c := newTestServer(t)

	// ~32 KB of padding in an ignored field, well under the 64 KB cap.
	body := append([]byte(`{"device_id":"device-under-cap","padding":"`),
		bytes.Repeat([]byte("a"), 32<<10)...)
	body = append(body, []byte(`"}`)...)

	req, err := http.NewRequest(http.MethodPost, c.base+"/v1/auth/anonymous", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("new request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	res, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("POST /v1/auth/anonymous: %v", err)
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		t.Errorf("a %d-byte body = %d, want 200", len(body), res.StatusCode)
	}
}

func TestAnonymousAuthRejectsOversizedDeviceID(t *testing.T) {
	c := newTestServer(t)

	// 129 characters: one past the cap, and comfortably inside the body limit,
	// so this proves the field check exists rather than the reader cap firing.
	code := c.do(http.MethodPost, "/v1/auth/anonymous",
		map[string]string{"device_id": strings.Repeat("d", 129)}, nil)
	if code != http.StatusBadRequest {
		t.Errorf("129-character device_id = %d, want 400", code)
	}

	// 128 is still fine.
	code = c.do(http.MethodPost, "/v1/auth/anonymous",
		map[string]string{"device_id": strings.Repeat("d", 128)}, nil)
	if code != http.StatusOK {
		t.Errorf("128-character device_id = %d, want 200", code)
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
	c.confirm(started)

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
	c.confirm(started)

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
