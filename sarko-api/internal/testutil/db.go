// Package testutil provides database fixtures for tests.
package testutil

import (
	"context"
	"os"
	"testing"

	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/Yasuslik/sarko-api/internal/db"
)

// Pool returns a migrated, empty database. It skips the test when
// TEST_DATABASE_URL is unset so a missing docker daemon never looks like a bug.
func Pool(t *testing.T) *pgxpool.Pool {
	t.Helper()

	url := os.Getenv("TEST_DATABASE_URL")
	if url == "" {
		t.Skip("TEST_DATABASE_URL is not set — run `make test-db` and re-run with make test")
	}

	if err := db.Migrate(url); err != nil {
		t.Fatalf("migrate: %v", err)
	}

	pool, err := db.Open(context.Background(), url)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	t.Cleanup(pool.Close)

	// Truncate rather than recreate: fast, and RESTART IDENTITY keeps ids predictable.
	_, err = pool.Exec(context.Background(),
		`TRUNCATE raid_sessions, garage_progress, stash_items, players RESTART IDENTITY CASCADE`)
	if err != nil {
		t.Fatalf("truncate: %v", err)
	}
	return pool
}
