package db

// These tests live in package db, not db_test, because they assert on
// migrationLockID. They deliberately do not use internal/testutil: testutil
// imports this package, so importing it back would be an import cycle.

import (
	"context"
	"database/sql"
	"fmt"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	_ "github.com/jackc/pgx/v5/stdlib"
)

func testURL(t *testing.T) string {
	t.Helper()
	url := os.Getenv("TEST_DATABASE_URL")
	if url == "" {
		t.Skip("TEST_DATABASE_URL is not set — run `make test-db` and re-run with make test")
	}
	return url
}

// scratchDatabase creates an empty database and returns its URL, dropping it
// when the test ends.
//
// Every lock assertion here needs a database with *pending* migrations: goose's
// Provider.Up checks HasPending before it takes the lock, and returns early
// when there is nothing to apply. That short-circuit is harmless (a boot with
// no pending migration writes nothing, so there is nothing to serialise) but
// it means the shared, already-migrated test database can never exercise the
// lock. A virgin database is also the exact case that used to crash-loop.
func scratchDatabase(t *testing.T) string {
	t.Helper()
	url := testURL(t)
	ctx := context.Background()

	admin, err := sql.Open("pgx", url)
	if err != nil {
		t.Fatalf("sql.Open: %v", err)
	}
	// Registered first so it runs last: the drop below still needs admin open.
	t.Cleanup(func() { _ = admin.Close() })

	// A unique name so a crashed earlier run cannot collide with this one.
	name := fmt.Sprintf("sarko_migrate_lock_%d", time.Now().UnixNano())
	if _, err := admin.ExecContext(ctx, `CREATE DATABASE `+name); err != nil {
		t.Skipf("cannot create a scratch database (%v) — skipping", err)
	}
	t.Cleanup(func() {
		// Terminate leftover sessions so DROP cannot hang on a pool that has
		// not finished closing.
		_, _ = admin.ExecContext(ctx,
			`SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = $1`, name)
		if _, err := admin.ExecContext(ctx, `DROP DATABASE IF EXISTS `+name); err != nil {
			t.Errorf("drop scratch database: %v", err)
		}
	})

	return replaceDatabase(t, url, name)
}

// advisoryLockHeld reports whether any session holds the migration advisory
// lock. A bigint advisory key is split across pg_locks.classid (high 32 bits)
// and pg_locks.objid (low 32 bits).
func advisoryLockHeld(t *testing.T, db *sql.DB) bool {
	t.Helper()
	var n int
	err := db.QueryRow(
		`SELECT count(*) FROM pg_locks
		 WHERE locktype = 'advisory'
		   AND ((classid::bigint << 32) | objid::bigint) = $1`, migrationLockID).Scan(&n)
	if err != nil {
		t.Fatalf("query pg_locks: %v", err)
	}
	return n > 0
}

// TestMigrateWaitsForTheAdvisoryLock proves mutual exclusion directly rather
// than by hoping a race shows up: the test holds the migration lock itself,
// checks Migrate is still blocked, releases it, and checks Migrate then
// finishes. Without the lock, Migrate would run the migration immediately
// while the test still held it — which is exactly the two-replica boot that
// used to leave the loser exiting non-zero in a crash loop.
func TestMigrateWaitsForTheAdvisoryLock(t *testing.T) {
	scratchURL := scratchDatabase(t)
	ctx := context.Background()

	holder, err := sql.Open("pgx", scratchURL)
	if err != nil {
		t.Fatalf("sql.Open: %v", err)
	}
	defer holder.Close()

	// A session-level advisory lock belongs to one connection, so the holder
	// must be a pinned *sql.Conn, not the pool.
	conn, err := holder.Conn(ctx)
	if err != nil {
		t.Fatalf("holder conn: %v", err)
	}
	defer conn.Close()

	if _, err := conn.ExecContext(ctx, `SELECT pg_advisory_lock($1)`, migrationLockID); err != nil {
		t.Fatalf("acquire advisory lock: %v", err)
	}
	released := false
	release := func() {
		if released {
			return
		}
		released = true
		if _, err := conn.ExecContext(ctx, `SELECT pg_advisory_unlock($1)`, migrationLockID); err != nil {
			t.Errorf("release advisory lock: %v", err)
		}
	}
	defer release()

	done := make(chan error, 1)
	go func() { done <- Migrate(scratchURL) }()

	select {
	case err := <-done:
		t.Fatalf("Migrate returned (%v) while the advisory lock was held — migrations are not serialised", err)
	case <-time.After(2 * time.Second):
		// Still blocked, which is the point.
	}

	// Nothing may have been applied while the lock was held.
	if tableExists(t, scratchURL, "raid_sessions") {
		t.Error("Migrate created tables while another session held the migration lock")
	}

	release()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("Migrate after the lock was released: %v", err)
		}
	case <-time.After(60 * time.Second):
		t.Fatal("Migrate did not finish within 60s of the advisory lock being released")
	}

	if !tableExists(t, scratchURL, "raid_sessions") {
		t.Error("Migrate reported success but the schema was not created")
	}
}

// TestMigrateReleasesTheAdvisoryLock catches a lock leaked past a successful
// migration, which would wedge every later boot rather than failing loudly.
func TestMigrateReleasesTheAdvisoryLock(t *testing.T) {
	scratchURL := scratchDatabase(t)

	if err := Migrate(scratchURL); err != nil {
		t.Fatalf("Migrate: %v", err)
	}

	observer, err := sql.Open("pgx", scratchURL)
	if err != nil {
		t.Fatalf("sql.Open: %v", err)
	}
	defer observer.Close()

	if advisoryLockHeld(t, observer) {
		t.Error("the migration advisory lock is still held after Migrate returned")
	}
}

// TestConcurrentMigrateOnVirginDatabase is the end-to-end version of the
// two-replica boot: four Migrate calls applying migration 0001 from nothing at
// the same time, all of which must succeed and apply it exactly once.
//
// It is a smoke test, not the proof. Removing the lock does not make it fail
// reliably — whether the losers collide depends on scheduling. The
// deterministic guarantee is TestMigrateWaitsForTheAdvisoryLock above, which
// fails immediately without the lock; this one catches the coarser mistakes
// (double-apply, an error surfacing to a caller) in a realistic shape.
func TestConcurrentMigrateOnVirginDatabase(t *testing.T) {
	scratchURL := scratchDatabase(t)
	ctx := context.Background()

	const replicas = 4
	var wg sync.WaitGroup
	errs := make([]error, replicas)
	start := make(chan struct{})
	for i := range replicas {
		wg.Add(1)
		go func() {
			defer wg.Done()
			<-start // release them together, to actually contend
			errs[i] = Migrate(scratchURL)
		}()
	}
	close(start)
	wg.Wait()

	for i, err := range errs {
		if err != nil {
			t.Errorf("replica %d: Migrate on a virgin database failed: %v", i, err)
		}
	}

	scratch, err := sql.Open("pgx", scratchURL)
	if err != nil {
		t.Fatalf("open scratch: %v", err)
	}
	defer scratch.Close()

	// Exactly one row per migration: a double-apply would show up here.
	var versions int
	if err := scratch.QueryRowContext(ctx,
		`SELECT count(*) FROM goose_db_version WHERE version_id > 0`).Scan(&versions); err != nil {
		t.Fatalf("count goose_db_version: %v", err)
	}
	if versions != 1 {
		t.Errorf("goose_db_version has %d applied rows, want 1", versions)
	}

	if !tableExists(t, scratchURL, "raid_sessions") {
		t.Error("raid_sessions was not created on the scratch database")
	}
}

func tableExists(t *testing.T, url, table string) bool {
	t.Helper()
	conn, err := sql.Open("pgx", url)
	if err != nil {
		t.Fatalf("sql.Open: %v", err)
	}
	defer conn.Close()

	var exists bool
	err = conn.QueryRow(
		`SELECT EXISTS (SELECT 1 FROM information_schema.tables
		                WHERE table_schema = 'public' AND table_name = $1)`, table).Scan(&exists)
	if err != nil {
		t.Fatalf("check table %s: %v", table, err)
	}
	return exists
}

// replaceDatabase swaps the database name in a postgres URL.
func replaceDatabase(t *testing.T, url, name string) string {
	t.Helper()
	scheme := "://"
	i := strings.Index(url, scheme)
	if i < 0 {
		t.Fatalf("TEST_DATABASE_URL %q has no scheme", url)
	}
	rest := url[i+len(scheme):]
	slash := strings.Index(rest, "/")
	if slash < 0 {
		t.Fatalf("TEST_DATABASE_URL %q has no database path", url)
	}
	tail := rest[slash+1:]
	query := ""
	if q := strings.Index(tail, "?"); q >= 0 {
		query = tail[q:]
	}
	return url[:i+len(scheme)] + rest[:slash+1] + name + query
}
