package db_test

import (
	"context"
	"strings"
	"testing"

	"github.com/Yasuslik/sarko-api/internal/testutil"
)

func TestMigrationsCreateTables(t *testing.T) {
	pool := testutil.Pool(t)
	ctx := context.Background()

	for _, table := range []string{
		"players", "stash_items", "garage_progress", "raid_sessions",
		// What the player is wearing (equipment spec §6). It is server state
		// because /v1/raid/start debits it, so its absence is not a cosmetic
		// failure — the loadout would silently become empty for everyone.
		"player_equipment",
	} {
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

// ВИЛАЗКА needs exactly one stored fact (spec §4.5), and this is it: which kind of
// run a session is. The cooldown is DERIVED from it — the newest closed sortie's
// closed_at — so a missing column is not a cosmetic failure: every sortie would
// read as an ordinary raid, and the free run would debit the stash, latch the
// tutorial and never be rate-limited.
func TestSortieModeColumnExists(t *testing.T) {
	pool := testutil.Pool(t)
	ctx := context.Background()

	var def *string
	err := pool.QueryRow(ctx,
		`SELECT column_default FROM information_schema.columns
		 WHERE table_schema = 'public' AND table_name = 'raid_sessions' AND column_name = 'mode'`,
	).Scan(&def)
	if err != nil {
		t.Fatalf("raid_sessions.mode must exist: %v", err)
	}
	// The default backfills every pre-existing session as a raid, which is what all
	// of them are — nothing before this migration could have been a sortie.
	if def == nil || !strings.Contains(*def, "raid") {
		t.Errorf("column_default = %v, want 'raid'", def)
	}

	var playerID string
	if err := pool.QueryRow(ctx,
		`INSERT INTO players (device_id) VALUES ('dev-mode') RETURNING id`).Scan(&playerID); err != nil {
		t.Fatalf("insert player: %v", err)
	}
	var mode string
	if err := pool.QueryRow(ctx,
		`INSERT INTO raid_sessions (player_id, map_id, seed, session_token_hash, loadout, expires_at)
		 VALUES ($1, 'bridge', 1, '\x00', '[]'::jsonb, now() + interval '1 minute')
		 RETURNING mode`, playerID).Scan(&mode); err != nil {
		t.Fatalf("insert session: %v", err)
	}
	if mode != "raid" {
		t.Errorf("a session inserted without a mode is %q, want raid", mode)
	}
}
