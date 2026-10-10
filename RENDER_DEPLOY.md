# RENDER_DEPLOY.md — deploying Hidimbi's Town on Render

Render does **not** read `docker-compose.yml`. Every service is deployed
individually from a Dockerfile. This file is the exact procedure.

You will end up with **five** Render resources:

| # | Render resource | Type | Gives you |
|---|---|---|---|
| 1 | `hidimbi-vault` | Managed PostgreSQL | nothing player-facing |
| 2 | `hidimbi-lake` | Web Service | **Lake API URL** |
| 3 | `hidimbi-flag` | Web Service | **Flag Service URL** |
| 4 | `hidimbi-rotator` | Background Worker | nothing |
| — | the two URLs | — | what you put in `description.md` |

---

## Step 0 — push the repo

Render deploys from a connected git repo, not a folder upload.

```bash
git init && git add . && git commit -m "hidimbi render deploy"
git branch -M main
git remote add origin <your-repo-url>
git push -u origin main
```

The repo root must contain `config.json` and the two folders `backend_lake/`
and `backend_flag/` — the layout as-is. If `config.json` changes later:

```bash
python3 tools/sync_render_config.py     # copies it into both folders
git add backend_lake/config.json backend_flag/config.json
git commit -m "sync render config" && git push
```

**Never edit the copies in `backend_lake/` or `backend_flag/` by hand.** They
must be byte-identical, because the vault selector is derived from
`config.json` by *both* the solver and the flagger. If they drift, stage 22
fails with "the chain did not hold together" and it looks like a solver bug.

---

## Step 1 — create the PostgreSQL database

Render dashboard → **New → PostgreSQL**.

- Name: `hidimbi-vault`
- Region: pick the one closest to your players (this matters for stage 20 —
  see the note at the bottom)
- Plan: whatever you like; this challenge stores a handful of rows

When it's created, open it and copy the **External Database URL**. You will
need it once, from your laptop, to run the init scripts.

### Run the init scripts (Render's Postgres has no initdb.d)

Render's managed PostgreSQL runs without the official image's
`/docker-entrypoint-initdb.d` hook, so `backend/db/init/*.sql` is **never
applied automatically**. You must apply it once by hand:

```bash
export DATABASE_URL="postgres://<user>:<pass>@<host>:5432/<dbname>"   # External URL
export DB_PASSWORD="$(openssl rand -hex 32)"      # becomes vault_flagger
export ROTATOR_PASSWORD="$(openssl rand -hex 32)" # becomes vault_rotator
python3 tools/render_init_vault.py
```

Expected output:

```
[*] 1/4 creating roles
[*] 2/4 applying schema and functions
[*] 3/4 setting runtime role passwords
[*] 4/4 applying least-privilege grants
[+] Render Vault initialised.
[*] flag rows are created lazily per team at the Stage-12 gate,
    so there is nothing to insert here.
```

It is idempotent — safe to re-run, and re-running never consumes a flag row.

Save `DB_PASSWORD` and `ROTATOR_PASSWORD` somewhere safe; you need them for
every service below.

---

## Step 2 — `hidimbi-lake` (this gives you the Lake API URL)

**New → Web Service** → connect the repo:

| Setting | Value |
|---|---|
| Root Directory | *(leave blank)* |
| Language | Docker |
| Dockerfile Path | `backend_lake/Dockerfile` |
| Docker Build Context Directory | `backend_lake` |
| Health Check Path | `/healthz` |
| Instance type | whatever; the whistle stage is CPU-light |

**Environment variables** (Render → Environment → add each):

```
HIDIMBI_ROLE=app
HIDIMBI_EXPOSE_INTERNAL=1
HIDIMBI_INSTANCE_ID=hidimbi-main
DB_HOST=<the vault's INTERNAL hostname from Step 1>
DB_PORT=5432
DB_NAME=<the vault's database name>
DB_USER=vault_flagger
DB_PASSWORD=<the DB_PASSWORD from Step 1>
FLAGGER_PROOF_KEY=<openssl rand -hex 32>
BACKEND_BEARER_SECRET=<openssl rand -hex 32>
COURIER_INTERNAL_TARGET=127.0.0.1:8100
```

`FLAGGER_PROOF_KEY` and `BACKEND_BEARER_SECRET` must be **identical** across
the lake and flag services. Generate each once, reuse it.

Render assigns the URL. That is your **Lake API URL**.

### What this container runs

`backend_lake/entrypoint.sh` starts two processes:

- `:8000` — the Lake API + Observatory + whistle + the SSRF courier (public)
- `:8100` — the Keeper's internal index (**not** published)

Render only routes the one declared port, so a player who tries
`https://<lake-url>:8100/internal/gate` gets nothing. But the courier runs
*inside this same container*, so it can reach `127.0.0.1:8100` — which is
exactly what stage 21 requires. This is why `COURIER_INTERNAL_TARGET` is set
to `127.0.0.1:8100`.

---

## Step 3 — `hidimbi-flag` (this gives you the Flag Service URL)

**New → Web Service** → connect the same repo:

| Setting | Value |
|---|---|
| Root Directory | *(leave blank)* |
| Language | Docker |
| Dockerfile Path | `backend_flag/Dockerfile` |
| Docker Build Context Directory | `backend_flag` |
| Health Check Path | `/healthz` |

**Environment variables**:

```
HIDIMBI_ROLE=flagger
HIDIMBI_EXPOSE_INTERNAL=0
HIDIMBI_INSTANCE_ID=hidimbi-main          <-- must match the lake service
DB_HOST=<same vault internal hostname>
DB_PORT=5432
DB_NAME=<same>
DB_USER=vault_flagger
DB_PASSWORD=<same DB_PASSWORD>
FLAGGER_PROOF_KEY=<the SAME key as the lake service>
```

Render assigns the URL. That is your **Flag Service URL**.

### Why this service is a separate image

`backend_flag/Dockerfile` **does not copy `server.py`**. So even if someone
compromised this container, there is no Lake API in it. And
`HIDIMBI_EXPOSE_INTERNAL=0` means `/internal/gate` is never registered, so the
public flag service does not carry the Stage-21 target path at all — not even
as a 404 a probe could confirm. The only place that route exists is the
unpublished `:8100` inside the lake container.

---

## Step 4 — `hidimbi-rotator` (Background Worker)

**New → Background Worker** → same repo:

| Setting | Value |
|---|---|
| Dockerfile Path | `backend/Dockerfile` |
| Docker Build Context Directory | `backend` |

**Environment variables**:

```
HIDIMBI_ROLE=rotator
DB_HOST=<vault internal hostname>
DB_PORT=5432
DB_NAME=<same>
DB_USER=vault_rotator
DB_PASSWORD=<ROTATOR_PASSWORD from Step 1>
INSTANCE_ID=hidimbi-main
TEAM_ID=solo
FLAG_TTL_HOURS=24
ROTATION_INTERVAL_SECONDS=3600
```

No public port. This loops: `vault.rotate()` expires the current epoch and
inserts a fresh one with a fresh flag, so a leaked selector has a hard TTL.

> Note: this rotates the `solo` row only, which is the row used for local
> testing. Real per-team rotation happens because `ensure_team_flag` mints a
> new epoch per team automatically when a team's row is consumed.

---

## Step 5 — put the two URLs in the challenge text

Edit `description.md`:

```markdown
- `Dimbapura.apk`
- https://<your-lake-service>.onrender.com     ← Lake API URL
- https://<your-flag-service>.onrender.com     ← Flag Service URL
```

That's what you publish. Players get **three things**: the APK and those two
URLs. No database connection string, no `config.json`, no credentials.

---

## Sanity checks after deploy

```bash
LAKE=https://<your-lake-service>.onrender.com
FLAG=https://<your-flag-service>.onrender.com

curl $LAKE/healthz                                   # {"status":"ok"}
curl $FLAG/healthz                                   # {"status":"ok"}
curl -X POST -d '{}' $FLAG/internal/gate             # 404 — good, route absent
curl $LAKE/api/v1/observatory/jwks.json              # the published JWK
curl -X POST -d '{"whistle":"00"}' $LAKE/api/v1/observatory/whistle   # a delay
```

Then run the full solver against the live stack from your laptop:

```bash
python3 solve_scripts/solve_hidimbi.py \
  --backend $LAKE --flag-service $FLAG --keeper http://127.0.0.1:9999 \
  --instance hidimbi-main --team solo
```

`--keeper` is unused against Render (the internal index is reached by SSRF,
not by you), so any placeholder works.

---

## Two things that will bite you

### 1. Stage 20 is network-sensitive

The timing oracle adds 12 ms per correctly matched leading byte. On loopback
that is ~3× the noise floor and the byte ranking is unambiguous. Over the
public internet — through Render's router, plus TLS — the step can be lost in
jitter, and the stage becomes unsolvable or wildly flaky.

Your options, best first:
- Pick the Render region closest to your players.
- Raise `whistle_delay_us` in `config.json` (50–100 ms makes the signal
  dominate jitter at the cost of a slower attack).
- Accept retries and keep the rate limit generous.

**Test this against the real deployment before you publish.** "It solved on my
laptop" is not evidence it solves over the internet.

### 2. Render free-tier services spin down

A free Web Service sleeps after inactivity. The first request after a sleep
takes ~30 s to respond, which will blow every ±60 s replay window and make
the timing attack unusable. Use a paid instance type for all four services
during the competition, or accept that the first solve attempt per window
fails.

---

## Mapping back to the docker-compose version

If you would rather move to a VPS later, `backend/docker-compose.yml` is
still valid and is the simpler topology — it just runs all four containers on
one box with Docker's internal DNS. `backend_lake/` and `backend_flag/` exist
only because Render needs one Dockerfile per service; the application code in
them is identical to `backend/`.
