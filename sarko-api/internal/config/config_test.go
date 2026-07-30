package config

import (
	"testing"
	"time"
)

func TestLoadRequiresSecrets(t *testing.T) {
	t.Setenv("DATABASE_URL", "")
	t.Setenv("JWT_SECRET", "")
	if _, err := Load(); err == nil {
		t.Fatal("expected error when DATABASE_URL and JWT_SECRET are missing")
	}
}

func TestLoadAppliesDefaults(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://x")
	t.Setenv("JWT_SECRET", "secret")
	t.Setenv("PORT", "")
	t.Setenv("RAID_TTL", "")
	t.Setenv("PENDING_TTL", "")
	t.Setenv("GRACE_BUFFER", "")

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
}

func TestLoadParsesGraceBuffer(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://x")
	t.Setenv("JWT_SECRET", "secret")
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
	t.Setenv("JWT_SECRET", "secret")
	t.Setenv("GRACE_BUFFER", "two minutes")

	if _, err := Load(); err == nil {
		t.Fatal("expected an error for an unparsable GRACE_BUFFER")
	}
}

func TestLoadParsesDurations(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://x")
	t.Setenv("JWT_SECRET", "secret")
	t.Setenv("RAID_TTL", "5m")

	c, err := Load()
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	if c.RaidTTL != 5*time.Minute {
		t.Errorf("RaidTTL = %v, want 5m", c.RaidTTL)
	}
}
