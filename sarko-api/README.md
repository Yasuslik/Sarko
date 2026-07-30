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

## Configuration

| Env | Default | Meaning |
|---|---|---|
| `PORT` | `8080` | listen port |
| `DATABASE_URL` | — | required |
| `JWT_SECRET` | — | required |
| `RAID_TTL` | `12m` | how long a confirmed raid may run |
| `PENDING_TTL` | `60s` | how long an unconfirmed raid holds its loadout |
| `GRACE_BUFFER` | `2m` | added to `RAID_TTL` at confirm time |

### Raid timing: the client timer must be shorter than `RAID_TTL`

When a raid is confirmed the server sets `expires_at = now() + RAID_TTL + GRACE_BUFFER`.
Once that deadline passes the session is closed as **died** and everything the player
carried and looted is lost, so the timing rule is not cosmetic:

- **The client's in-raid timer must be strictly shorter than `RAID_TTL`.** If the two
  are equal, a player who extracts on the last second can lose the whole run to a
  couple of seconds of network delay.
- **`GRACE_BUFFER` is slack for a slow result submission, not extra play time.** It
  covers the gap between the client deciding the raid is over and
  `POST /v1/raid/result` reaching the server. It is not a licence to keep playing,
  and the client must never present it as remaining time.
- **The confirm response is the source of truth.** `POST /v1/raid/confirm` returns
  `200 {"expires_at": "<RFC 3339>"}` — the deadline actually stored on the row. The
  client should align its countdown to that value rather than to its own copy of
  `RAID_TTL`, which may be stale after a config change.

## Endpoints

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/healthz` | — | liveness |
| POST | `/v1/auth/anonymous` | — | device id → JWT |
| GET | `/v1/profile` | Bearer | stash, garage tier, unlocked maps |
| POST | `/v1/raid/start` | Bearer | debit loadout, open session, return one-time token + seed |
| POST | `/v1/raid/confirm` | Bearer | mark the raid actually entered; returns `expires_at` |
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
