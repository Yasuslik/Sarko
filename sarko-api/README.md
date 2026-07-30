# sarko-api

Authoritative backend for [Sarko](../docs/superpowers/specs/2026-07-29-sarko-raid-slice-design.md):
stash, garage progression and raid session accounting.

## Run locally

    make test-db                       # postgres on :5455
    export DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable"
    export JWT_SECRET="dev-secret-change-me"
    make run

## Test

    make test                          # starts the test database, then go test ./...

Tests run packages serially (`-p 1`) because every DB-backed package shares one test
database and truncates it in testutil.Pool. Go's default parallel package execution
causes flakes (~40-50% once three packages do DB work). Tests that need Postgres skip
themselves when `TEST_DATABASE_URL` is unset.

## Endpoints

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/healthz` | — | liveness |
| POST | `/v1/auth/anonymous` | — | device id → JWT |
| GET | `/v1/profile` | Bearer | stash, garage tier, unlocked maps |
| POST | `/v1/raid/start` | Bearer | debit loadout, open session, return one-time token + seed |
| POST | `/v1/raid/confirm` | Bearer | mark the raid actually entered |
| POST | `/v1/raid/result` | Bearer | close the raid, credit survivors (idempotent) |
| POST | `/v1/garage/craft` | Bearer | spend parts, advance one vehicle tier |

## Why it is dupe-proof

1. The loadout is debited when the raid **starts**, so one rifle cannot enter two raids.
2. `raid_session_token` is random, stored only as a SHA-256 hash, and works once.
3. `POST /v1/raid/result` is idempotent by session id — a retry after a dropped
   connection replays the stored answer instead of crediting twice.
4. A partial unique index makes two open raids per player impossible in the database.
5. A background sweeper closes abandoned raids: unconfirmed ones return the loadout,
   confirmed ones count as death.

When a real dedicated server exists it holds `raid_session_token` instead of the client,
and the client loses the ability to lie about outcomes. No schema change is needed.
