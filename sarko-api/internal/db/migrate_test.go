package db_test

import (
	"context"
	"testing"

	"github.com/Yasuslik/sarko-api/internal/testutil"
)

func TestMigrationsCreateTables(t *testing.T) {
	pool := testutil.Pool(t)
	ctx := context.Background()

	for _, table := range []string{"players", "stash_items", "garage_progress", "raid_sessions"} {
		var exists bool
		err := pool.QueryRow(ctx,
			`SELECT EXISTS (SELECT 1 FROM information_schema.tables
			                WHERE table_schema = 'public' AND table_name = $1)`, table).Scan(&exists)
		if err != nil {
			t.Fatalf("query %s: %v", table, err)
		}
		if !exists {
			t.Errorf("table %s does not exist", table)
		}
	}
}

func TestOneOpenRaidPerPlayerIsEnforced(t *testing.T) {
	pool := testutil.Pool(t)
	ctx := context.Background()

	var playerID string
	if err := pool.QueryRow(ctx,
		`INSERT INTO players (device_id) VALUES ('dev-1') RETURNING id`).Scan(&playerID); err != nil {
		t.Fatalf("insert player: %v", err)
	}

	insert := `INSERT INTO raid_sessions (player_id, map_id, seed, session_token_hash, loadout, expires_at)
	           VALUES ($1, 'bridge', 1, '\x00', '[]'::jsonb, now() + interval '1 minute')`

	if _, err := pool.Exec(ctx, insert, playerID); err != nil {
		t.Fatalf("first raid should insert: %v", err)
	}
	if _, err := pool.Exec(ctx, insert, playerID); err == nil {
		t.Fatal("second open raid for the same player must be rejected by the unique index")
	}
}
