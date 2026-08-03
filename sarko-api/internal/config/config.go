// Package config loads service configuration from the environment.
package config

import (
	"errors"
	"fmt"
	"os"
	"time"
)

// MinJWTSecretBytes is the floor for JWT_SECRET. HS256 tokens here live for 30
// days, so anyone holding one token can brute-force a short secret offline at
// their leisure and then mint a token for any player id. 32 bytes matches the
// HMAC-SHA256 block security level; below it, Load refuses to start rather
// than signing with a value like "x".
const MinJWTSecretBytes = 32

// Config holds every runtime setting. Load fails rather than guessing secrets.
type Config struct {
	Port        string
	DatabaseURL string
	JWTSecret   []byte
	// RaidTTL is how long a confirmed raid may run before the sweeper closes it as died.
	RaidTTL time.Duration
	// PendingTTL is how long an unconfirmed raid survives before its loadout is returned.
	PendingTTL time.Duration
	// GraceBuffer is added to RaidTTL when a raid is confirmed, so a result that
	// leaves the device just before the client's own timer expires is still
	// credited if the network delays it. It is slack for a slow submission, not
	// extra play time: the client's in-raid timer must stay shorter than RaidTTL.
	GraceBuffer time.Duration
	// SortieTTL is RaidTTL for a ВИЛАЗКА, and it is SHORTER on purpose (spec §4.5:
	// "the trade-off is quality, not danger ... the clock is shorter than a normal
	// raid"). The client's in-raid clock is already min(map duration, server deadline
	// − margin), so this is the one place the shorter run is decided, and no client
	// change is needed to honour it.
	//
	// A shorter clock makes a sortie WORSE, never safer. Nothing in this config makes
	// a free run less dangerous than a paid one, and nothing should.
	SortieTTL time.Duration
	// SortieCooldown is how long after a sortie ends before the next one may start,
	// which is the whole of "so it cannot be farmed". Server-owned and server-timed:
	// the client is told the remaining seconds so a button can display them, and
	// /v1/raid/start refuses by name however stale that number has become.
	SortieCooldown time.Duration
}

func Load() (Config, error) {
	c := Config{
		Port:        envOr("PORT", "8080"),
		DatabaseURL: os.Getenv("DATABASE_URL"),
		JWTSecret:   []byte(os.Getenv("JWT_SECRET")),
	}

	var err error
	if c.RaidTTL, err = envDuration("RAID_TTL", 12*time.Minute); err != nil {
		return Config{}, err
	}
	if c.PendingTTL, err = envDuration("PENDING_TTL", 60*time.Second); err != nil {
		return Config{}, err
	}
	if c.GraceBuffer, err = envDuration("GRACE_BUFFER", 2*time.Minute); err != nil {
		return Config{}, err
	}
	// Six minutes against RAID_TTL's twelve: half a raid, which is short enough to be
	// felt as a limitation and long enough to reach an extraction on the shipped map.
	if c.SortieTTL, err = envDuration("SORTIE_TTL", 6*time.Minute); err != nil {
		return Config{}, err
	}
	// Fifteen minutes from the END of the last sortie. Longer than a sortie itself, so
	// the free run cannot be the main loop; short enough that a player who lost
	// everything is not locked out of the game for an evening.
	if c.SortieCooldown, err = envDuration("SORTIE_COOLDOWN", 15*time.Minute); err != nil {
		return Config{}, err
	}

	if c.DatabaseURL == "" {
		return Config{}, errors.New("DATABASE_URL is required")
	}
	if len(c.JWTSecret) == 0 {
		return Config{}, errors.New("JWT_SECRET is required")
	}
	if len(c.JWTSecret) < MinJWTSecretBytes {
		return Config{}, fmt.Errorf(
			"JWT_SECRET must be at least %d bytes, got %d: it signs 30-day player tokens, "+
				"and a short secret is brute-forceable offline from any single issued token",
			MinJWTSecretBytes, len(c.JWTSecret))
	}
	return c, nil
}

func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func envDuration(key string, fallback time.Duration) (time.Duration, error) {
	v := os.Getenv(key)
	if v == "" {
		return fallback, nil
	}
	d, err := time.ParseDuration(v)
	if err != nil {
		return 0, fmt.Errorf("%s: %w", key, err)
	}
	return d, nil
}
