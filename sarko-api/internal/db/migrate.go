package db

import (
	"context"
	"database/sql"
	"embed"
	"fmt"
	"io/fs"

	_ "github.com/jackc/pgx/v5/stdlib" // database/sql driver for goose
	"github.com/pressly/goose/v3"
	"github.com/pressly/goose/v3/lock"
)

//go:embed migrations/*.sql
var migrations embed.FS

// migrationLockID is an arbitrary but fixed key for the Postgres session-level
// advisory lock that serialises Migrate. Any process using the same database
// must use this same number; nothing else in Postgres shares the advisory lock
// namespace with it.
const migrationLockID int64 = 7264871276001

// Migrate applies every pending migration. Safe to call on every boot, and
// safe to call from several replicas booting at once.
//
// main.go migrates on every boot, so two replicas starting together would
// otherwise race on goose_db_version: the loser gets a duplicate-key or
// missing-relation error, run() returns it, and the process exits non-zero.
// That is a crash loop the first time this service is scaled past one
// instance. A session-level advisory lock makes the second replica wait for
// the first, then find nothing left to apply.
//
// The lock and the migration must run on the *same* connection or the lock
// protects nothing: if the pool hands the migration a different session, the
// lock-holding session can be reset or closed underneath it and another
// replica may start migrating while this one is mid-flight. goose's Provider
// is what makes that guarantee available — it takes one *sql.Conn, runs the
// session locker on it, runs the migrations on it, and releases the lock in a
// cleanup func. Doing it by hand is not possible with the pinned goose
// version, because goose.Up only accepts a *sql.DB.
//
// Using the Provider also stops Migrate from mutating goose's package-level
// globals (SetBaseFS/SetLogger/SetDialect) on every call.
//
// One goose behaviour worth knowing: Provider.Up checks HasPending *before* it
// takes the lock and returns early when there is nothing to apply. That is
// safe — a boot with no pending migration writes nothing, so there is nothing
// to serialise — but it means the lock is only ever contended on the boot that
// actually migrates, which is the boot that matters.
func Migrate(url string) error {
	ctx := context.Background()

	sqlDB, err := sql.Open("pgx", url)
	if err != nil {
		return fmt.Errorf("sql.Open: %w", err)
	}
	defer sqlDB.Close()

	// The provider is given the migrations directory itself, so migration
	// names match what the old goose.Up(sqlDB, "migrations") recorded.
	migrationsFS, err := fs.Sub(migrations, "migrations")
	if err != nil {
		return fmt.Errorf("migrations sub-fs: %w", err)
	}

	// pg_try_advisory_lock in a retry loop rather than a blocking
	// pg_advisory_lock: a replica that cannot get the lock eventually gives up
	// with an error instead of hanging on the boot path forever.
	locker, err := lock.NewPostgresSessionLocker(lock.WithLockID(migrationLockID))
	if err != nil {
		return fmt.Errorf("migration locker: %w", err)
	}

	provider, err := goose.NewProvider(goose.DialectPostgres, sqlDB, migrationsFS,
		goose.WithSessionLocker(locker),
		goose.WithLogger(goose.NopLogger()),
	)
	if err != nil {
		return fmt.Errorf("goose provider: %w", err)
	}

	if _, err := provider.Up(ctx); err != nil {
		return fmt.Errorf("goose up: %w", err)
	}
	return nil
}
