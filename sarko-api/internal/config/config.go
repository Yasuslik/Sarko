// Package config loads service configuration from the environment.
package config

import (
	"errors"
	"fmt"
	"os"
	"time"
)

// Config holds every runtime setting. Load fails rather than guessing secrets.
type Config struct {
	Port        string
	DatabaseURL string
	JWTSecret   []byte
	// RaidTTL is how long a confirmed raid may run before the sweeper closes it as died.
	RaidTTL time.Duration
	// PendingTTL is how long an unconfirmed raid survives before its loadout is returned.
	PendingTTL time.Duration
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

	if c.DatabaseURL == "" {
		return Config{}, errors.New("DATABASE_URL is required")
	}
	if len(c.JWTSecret) == 0 {
		return Config{}, errors.New("JWT_SECRET is required")
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
