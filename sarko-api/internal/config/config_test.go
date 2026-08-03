package config

import (
	"strings"
	"testing"
	"time"
)

// validSecret is the shortest JWT_SECRET Load accepts, so every test that just
// needs Load to succeed also documents the floor.
var validSecret = strings.Repeat("s", MinJWTSecretBytes)

func TestLoadRequiresSecrets(t *testing.T) {
	t.Setenv("DATABASE_URL", "")
	t.Setenv("JWT_SECRET", "")
	if _, err := Load(); err == nil {
		t.Fatal("expected error when DATABASE_URL and JWT_SECRET are missing")
	}
}

// TestLoadRejectsShortJWTSecret pins the startup floor. Load only checked
// non-empty before, so JWT_SECRET=x booted a service signing 30-day tokens
// with a one-byte key — recoverable offline from any single issued token, and
// then every player id is forgeable. This must fail loudly at boot, not at
// audit time.
func TestLoadRejectsShortJWTSecret(t *testing.T) {
	cases := []struct {
		name   string
		secret string
		wantOK bool
	}{
		{"single character", "x", false},
		{"one byte short of the floor", strings.Repeat("s", MinJWTSecretBytes-1), false},
		{"exactly the floor", strings.Repeat("s", MinJWTSecretBytes), true},
		{"comfortably over the floor", strings.Repeat("s", MinJWTSecretBytes*2), true},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			t.Setenv("DATABASE_URL", "postgres://x")
			t.Setenv("JWT_SECRET", tc.secret)

			_, err := Load()
			if tc.wantOK && err != nil {
				t.Fatalf("Load: %v", err)
			}
			if !tc.wantOK {
				if err == nil {
					t.Fatalf("a %d-byte JWT_SECRET must be rejected", len(tc.secret))
				}
				// The operator has to be able to act on the message.
				if !strings.Contains(err.Error(), "JWT_SECRET") {
					t.Errorf("error %q does not name JWT_SECRET", err)
				}
				// The secret itself must never appear in a startup error.
				// Only checked for secrets long enough that a match cannot be
				// an incidental substring of the message.
				if len(tc.secret) >= 8 && strings.Contains(err.Error(), tc.secret) {
					t.Errorf("error message leaks the secret: %q", err)
				}
			}
		})
	}
}

func TestLoadAppliesDefaults(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://x")
	t.Setenv("JWT_SECRET", validSecret)
	t.Setenv("PORT", "")
	t.Setenv("RAID_TTL", "")
	t.Setenv("PENDING_TTL", "")
	t.Setenv("GRACE_BUFFER", "")
	t.Setenv("SORTIE_TTL", "")
	t.Setenv("SORTIE_COOLDOWN", "")

	c, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if c.Port != "8080" {
		t.Errorf("Port = %q, want 8080", c.Port)
	}
	if c.RaidTTL != 12*time.Minute {
		t.Errorf("RaidTTL = %v, want 12m", c.RaidTTL)
	}
	if c.PendingTTL != 60*time.Second {
		t.Errorf("PendingTTL = %v, want 60s", c.PendingTTL)
	}
	if c.GraceBuffer != 2*time.Minute {
		t.Errorf("GraceBuffer = %v, want 2m", c.GraceBuffer)
	}
	// The sortie's clock is SHORTER than a raid's, and that is the design rule rather
	// than a coincidence of two defaults: spec §4.5's trade-off is quality and clock,
	// never safety. A SORTIE_TTL raised past RAID_TTL would make the free run the
	// better one, so the relation is asserted and not just the value.
	if c.SortieTTL != 6*time.Minute {
		t.Errorf("SortieTTL = %v, want 6m", c.SortieTTL)
	}
	if c.SortieTTL >= c.RaidTTL {
		t.Errorf("SortieTTL %v must be shorter than RaidTTL %v", c.SortieTTL, c.RaidTTL)
	}
	// And the cooldown outlasts the sortie itself, so the free run cannot be the main
	// loop however fast it is played.
	if c.SortieCooldown != 15*time.Minute {
		t.Errorf("SortieCooldown = %v, want 15m", c.SortieCooldown)
	}
	if c.SortieCooldown <= c.SortieTTL {
		t.Errorf("SortieCooldown %v must outlast SortieTTL %v, or sorties chain back to back",
			c.SortieCooldown, c.SortieTTL)
	}
}

func TestLoadParsesGraceBuffer(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://x")
	t.Setenv("JWT_SECRET", validSecret)
	t.Setenv("GRACE_BUFFER", "45s")

	c, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if c.GraceBuffer != 45*time.Second {
		t.Errorf("GraceBuffer = %v, want 45s", c.GraceBuffer)
	}
}

func TestLoadRejectsUnparsableGraceBuffer(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://x")
	t.Setenv("JWT_SECRET", validSecret)
	t.Setenv("GRACE_BUFFER", "two minutes")

	if _, err := Load(); err == nil {
		t.Fatal("expected an error for an unparsable GRACE_BUFFER")
	}
}

func TestLoadParsesDurations(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://x")
	t.Setenv("JWT_SECRET", validSecret)
	t.Setenv("RAID_TTL", "5m")

	c, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if c.RaidTTL != 5*time.Minute {
		t.Errorf("RaidTTL = %v, want 5m", c.RaidTTL)
	}
}
