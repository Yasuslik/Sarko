# sarko-api

Authoritative backend for [Sarko](../docs/superpowers/specs/2026-07-29-sarko-raid-slice-design.md):
stash, garage progression and raid session accounting.

## Run locally

    make test-db                       # postgres on :5455
    export DATABASE_URL="postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable"
    export JWT_SECRET="$(openssl rand -base64 48)"   # must be >= 32 bytes
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
| `JWT_SECRET` | — | required, **at least 32 bytes** |
| `RAID_TTL` | `12m` | how long a confirmed raid may run |
| `PENDING_TTL` | `60s` | how long an unconfirmed raid holds its loadout |
| `GRACE_BUFFER` | `2m` | added to `RAID_TTL` at confirm time |
| `SORTIE_TTL` | `6m` | `RAID_TTL` for a ВИЛАЗКА — **must stay shorter than `RAID_TTL`** |
| `SORTIE_COOLDOWN` | `15m` | how long after a sortie *ends* before another may start |

`JWT_SECRET` must be at least 32 bytes or the process refuses to start. It signs
30-day HS256 player tokens, so a short secret can be brute-forced offline from any
single issued token, after which every player id is forgeable. Generate it
(`openssl rand -base64 48`), do not type it.

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

### ВИЛАЗКА, the free run (`mode: "sortie"`)

`POST /v1/raid/start` takes an optional `"mode"`: `raid` (the default, and what an
omitted field means) or `sortie`. That one string is the **only** thing a client says
about a free run. Everything it implies is decided here:

- **Free.** The request's `loadout` is discarded unread; nothing is debited.
- **The server grants the kit**, rolled from `domain.SortieKits` with the service's
  own generator, and returned as `granted_kit` so the client can show what it was
  lent. There is no field a client could use to ask for a better one.
- **Extraction credits the kit and the haul; death credits neither.** The kit is
  written to `raid_sessions.loadout`, so the existing extraction path returns it —
  "what you extract is yours" is the same code that returns a paid raid's loadout.
- **A shorter clock**, `SORTIE_TTL` instead of `RAID_TTL`, picked at confirm time from
  the session's own stored mode. A sortie is *worse*, never safer.
- **A cooldown**, `SORTIE_COOLDOWN` from the moment the last sortie ended, checked
  inside the transaction that holds the player's row lock. Too early is
  `409 sortie_cooldown` with the remaining seconds in the message. `GET /v1/profile`
  reports `sortie_cooldown_seconds` so a button can draw a countdown; that number
  decides nothing.
- **It does not latch `tutorial_completed`.** Only an ordinary raid's extraction does.

A sortie death does *not* clear `player_equipment`: nothing of the player's was
debited, so their own gear is still in their stash and still theirs.

## Endpoints

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/healthz` | — | liveness |
| POST | `/v1/auth/anonymous` | — | device id → JWT |
| GET | `/v1/profile` | Bearer | stash, garage tier, unlocked maps |
| POST | `/v1/raid/start` | Bearer | debit loadout, open session, return one-time token + seed. `mode: "sortie"` makes it free and server-kitted (see above) |
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

6. `POST /v1/raid/confirm` and `POST /v1/raid/result` check that the session belongs to
   the authenticated player, so a leaked session token is not on its own enough to drive
   somebody else's raid.

When a real dedicated server exists it holds `raid_session_token` instead of the client,
and the client loses the ability to lie about outcomes. No schema change is needed.

## Known constraints

**Confirm and result are tied to a player JWT.** Point 5 above means both endpoints now
require a `Bearer` player token *and* a session token, and reject the call unless
`raid_sessions.player_id` matches the authenticated player.

When the dedicated game server described above is built, it — not the client — will hold
the session token, and it has no player JWT to present. It will therefore need a
service-auth path to these two endpoints: either a service credential that is allowed to
act for a stated player id, or an internal variant of the two handlers that authenticates
the game server and takes the player id from the session row.

This is a known, accepted prerequisite of the ownership hardening, recorded here so it is
a planned task rather than a surprise. **The database schema is unaffected** — the
constraint is entirely in the API's authentication layer.
