# Flags.md — Flag Values, Formats & Validation Rules

## Purpose of this document

This file is the single source of truth for every flag-shaped string, cryptographic key component, nonce, and validation artifact across the challenge — real, decoy, and intermediate proof materials. Keep this file strictly private; do not distribute it to participants.

**The real flag is not recorded here.** It exists solely as a row in the external
PostgreSQL Vault container. Nothing in this repository — this file included —
holds it.

---

## Global flag format

All legitimate submittable flags strictly follow:

```
KCTF{HIDIMBI_TOWN_<UNIQUE_SUFFIX>}
```

`<UNIQUE_SUFFIX>` is 12 characters drawn from the CSPRNG-generated alphabet
`ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (no `I`, `O`, `0`, `1`), produced **inside the
database container** by `vault.provision()`. The suffix is never written to
`config.json`, to any image, or to any file.

Decoy flags use alternate prefixes (`FLAG{...}`, `SETU{...}`, `KCTF{DECOY...}`):

- `FLAG{HIDIMBI_IS_WATCHING}` — `strings.xml`, and the salt for the Stage-3 token
- `SETU{FOREST_PATH_COMPLETE}` — anti-analysis shadow path
- `SETU{LAKE_REFLECTION_COMPLETE}` — Lake Intent trap
- `KCTF{DECOY_CAVE_ILLUSION_OF_MAYA}` — naive Cave query

---

## Where the real flag lives

```
container: hidimbi_vault (postgres:15-alpine)
schema  : vault
table   : vault.flag_instances
column  : flag_value
```

The row is created by provisioning and is readable **only** through
`vault.claim_by_selector(instance_id, team_id, selector_sha, client_ip)`, which
the `vault_flagger` role may `EXECUTE` and nothing else:

```sql
-- exact grants (see backend/db/init/02_roles.sql)
GRANT EXECUTE ON FUNCTION
    vault.claim_by_selector(text, text, bytea, inet) TO vault_flagger;
GRANT EXECUTE ON FUNCTION
    vault.stub_for_team(text, text)              TO vault_flagger;
GRANT EXECUTE ON FUNCTION
    vault.log_rejection(text,text,text,text,bytea,inet) TO vault_flagger;
REVOKE ALL ON ALL TABLES IN SCHEMA vault FROM vault_flagger;
-- and ROW LEVEL SECURITY is enabled on vault.flag_instances with no policy,
-- i.e. default-deny for every non-owner role.
```

The flagging service connects with `HIDIMBI_FLAGGER_DB_*` (role `vault_flagger`),
which is injected by the platform and never appears in the repository or in an
image layer.

### Exact schema

```sql
CREATE TABLE vault.flag_instances (
    instance_id   text        NOT NULL,
    team_id       text        NOT NULL,
    epoch_id      integer     NOT NULL DEFAULT 1,
    selector_sha  bytea       NOT NULL,                 -- 32 bytes
    flag_value    text        NOT NULL,
    created_at    timestamptz NOT NULL DEFAULT now(),
    expires_at    timestamptz,                          -- NULL = never expires
    consumed      boolean     NOT NULL DEFAULT false,
    consumed_at   timestamptz,
    CONSTRAINT flag_instances_pkey PRIMARY KEY (instance_id, team_id, epoch_id),
    CONSTRAINT selector_is_32_bytes   CHECK (octet_length(selector_sha) = 32),
    CONSTRAINT flag_value_kctf        CHECK (flag_value LIKE 'KCTF{%}')
);

-- exactly one live (unconsumed, unexpired) row per team
CREATE UNIQUE INDEX flag_instances_live_unique
    ON vault.flag_instances (instance_id, team_id)
    WHERE consumed = false AND expires_at IS NULL;

CREATE TABLE vault.flag_redemptions (       -- one-time ledger
    instance_id text, team_id text, epoch_id integer,
    selector_sha bytea, redeemed_at timestamptz DEFAULT now(), client_ip inet,
    PRIMARY KEY (instance_id, team_id, epoch_id)
);

CREATE TABLE vault.flag_rejections (        -- audit trail
    id bigserial PRIMARY KEY, instance_id text, team_id text,
    reason text NOT NULL, detail text, selector_sha bytea, client_ip inet,
    attempted_at timestamptz DEFAULT now()
);
```

### The exact query

The flag service's Stage-22 handler runs exactly one SQL call, parameterised by
the selector the solver derived:

```sql
SELECT vault.claim_by_selector(
    %s,                 -- instance_id  (from the flagger's own env)
    %s,                 -- team_id      (from the JWT stamp / claim header)
    %s::bytea,          -- the player's 32-byte selector, hex-decoded
    %s::inet            -- client IP
);
```

`vault.claim_by_selector` is a single atomic PL/pgSQL function. It:

1. `SELECT ... FOR UPDATE` on the matching row,
2. raises `SELECTOR_MISMATCH` (`P0001`) if nothing matched,
3. raises `FLAG_EXPIRED_OR_CONSUMED` (`P0002`) if the row is not live,
4. marks the ticket and the row consumed,
5. inserts into `vault.flag_redemptions`,
6. `RETURN`s `flag_value`.

Because steps 4–5 are in the same transaction with a row lock, exactly one
concurrent caller can ever observe the flag.

### Auth flow

```
stage 22 input         = { selector, proof, seal }

proof  = HMAC-SHA256( K1||K2||K3||K4, selector || instance_id || team_id )
seal   = HMAC-SHA256( FLAGGER_KEY, selector || instance_id || team_id || epoch )
         ^ minted by the flagger's INTERNAL index, reachable only via SSRF

flagger, in order:
  1. constant-time compare proof
  2. resolve the live epoch from the vault (never from config), reject if the
     solver's epoch hint has been retired
  3. constant-time compare seal
  4. constant-time compare the player's selector against the recomputed one
  5. SQL read parameterised by the player's selector bytes
  6. return { flag } from the database, or 503 with nothing if the DB is down
```

---

## Challenge — Hidimbi's Town: Secrets & Artifact Registry

| Component / Symbol | Format / Size | Value / Generation Rule | Location & Role |
| :--- | :--- | :--- | :--- |
| **Real Flag** | String | `KCTF{HIDIMBI_TOWN_<12 CSPRNG chars>}` | `vault.flag_instances.flag_value` in the external PostgreSQL container only. |
| **Decoy Flag 1 (Static)** | String | `FLAG{HIDIMBI_IS_WATCHING}` | `strings.xml`; salt for Stage 3. |
| **Decoy Flag 2 (Anti-Analysis)** | String | `SETU{FOREST_PATH_COMPLETE}` | `libtown.so` shadow path. |
| **Decoy Flag 3 (Intent Trap)** | String | `SETU{LAKE_REFLECTION_COMPLETE}` | `TownActivity` Lake handler. |
| **Decoy Flag 4 (Storage Trap)** | String | `KCTF{DECOY_CAVE_ILLUSION_OF_MAYA}` | `cave_store.bin` default record. |
| **True Component Graph** | 6 node IDs | `17→29→4→8→13→31` | transition vector `(node + dir) % 37`. |
| **Visual / Decoy Graph** | 6 node IDs | `17→4→29→13→8→31` | the map artwork. |
| **Partial Keys K1–K4** | 16 B each | from config.json | Stages 5, 6, 7, 8. |
| **Nonces N1–N4** | 16 B each | from config.json | Stages 5, 6, 7, 8. |
| **Combined HMAC Key** | 64 B | `K1‖K2‖K3‖K4` | Stages 9/10/22 proof. |
| **Attestation** | JWT string | RS256, constant claims | Stage 12; keys the Stage-13 mosaic. |
| **scriptorium_seed** | 32 B | from config.json | Stage 13 payload. |
| **scriptorium_mark** | 16 B | VM output `R0‖R1‖R2‖R3` | Stage 14 output. |
| **drum_key** | 32 B | from config.json | Stage 15 format-string leak. |
| **scribe_rune** | 16 B | from config.json | Stage 16 protobuf OOB. |
| **granary_secret** | 16 B | from config.json | Stage 17 TOCTOU race. |
| **curve secret** | int | `{0, 1}` after confinement | Stage 19 small subgroup. |
| **whistle_key** | 8 B | from config.json | Stage 20 timing side channel. |
| **Vault selector** | 32 B | keyed SHA-256 over the whole chain | Stage 22 credential. |

---

## Failure, Uniqueness & Rotation

### Fail closed

| Condition | Behaviour | Player-visible output |
| :--- | :--- | :--- |
| PostgreSQL unreachable | `503` | `{"status":"VAULT_UNAVAILABLE","message":"No flag is issued."}` |
| Connection pool exhausted | `503` | as above |
| Any unexpected `psycopg2.PGError` | `503` (global handler) | as above |
| Business-rule rejection | `403`/`404` | generic refusal, plus an audit row |
| Flag row already consumed | `403` | "The vault refused this claim." |
| Epoch retired | `403` | "That epoch has been retired." |

There is **no cached flag, no config-backed flag, and no partial output** on any
failure path. A `SELECTOR_MISMATCH` raised inside the procedure is recorded in
`vault.flag_rejections` and answered with a generic refusal, not with the
database's own error text.

### Per-instance / per-team uniqueness

The primary key is `(instance_id, team_id, epoch_id)` and the selector is a keyed
hash over `instance_id` and `team_id` as well as the chain material, so:

- two deployments of the same challenge (two `HIDIMBI_INSTANCE_ID` values) share
  nothing, and
- two teams inside one deployment have different selectors and different rows.

### Rotation

- `vault.rotate(instance_id, team_id, ttl_hours)` expires the live row and
  inserts `epoch_id + 1` with a fresh flag and (optionally) a fresh selector.
- The rotator runs as its own container (`hidimbi_rotator`, role
  `vault_rotator`, `EXECUTE` on `vault.rotate` only) on the schedule set by
  `HIDIMBI_ROTATION_CRON`.
- The flagger resolves the live epoch *from the vault* on every claim, so a
  rotation can never leave it validating against a retired epoch.
- Default TTL: `HIDIMBI_FLAG_TTL_HOURS` (24 in the shipped compose file).

---

## Validation & Anti-Cheating Invariants

1. **One-Time Flag Claim:** `vault.claim_by_selector` is a single atomic
   transaction with `FOR UPDATE`; a selector can only be spent once per
   `(instance, team, epoch)`.
2. **Replay Protection:** the Stage-9 proof still requires a ±60 s timestamp
   combined with `SHA256(ANDROID_ID || K4)`; the Stage-22 selector is one-shot.
3. **Decoy Rejection:** all decoys (`FLAG{...}`, `SETU{...}`,
   `KCTF{DECOY...}`) are explicitly invalid in the scoring engine, and the
   anti-analysis shadow path poisons intermediate keys.
4. **No Static Leakage:** the real flag is not in the APK, in `cave_store.bin`,
   in `mosaic_wells.bin`, in `config.json`, in any Python source, in any shell
   script, or in any image layer. Grepping the repository for a live flag value
   returns nothing.
5. **Least Privilege:** three separate database roles — `vault_owner`
   (provisioning), `vault_flagger` (one read function), `vault_rotator`
   (one rotate function). The flag service cannot `SELECT` any table.

---

## Architecture & Thinking Notes (Chat Decisions & Rationale)

- **Moving the flag out of config.json was the point.** In the 12-stage version
  the flag service read the flag from its own config, which meant an image dump
  or a container-exec was enough. Now the only read path is a SQL call whose
  authorisation comes out of the exploit chain.
- **`SECURITY DEFINER` everywhere.** Every vault function runs as the owner, so
  the narrow grants are actually enforceable and RLS can default-deny the table.
- **The rejection table doubles as telemetry.** Because the flagger logs *why* a
  claim failed (proof, seal, selector, epoch), organizers can see whether a team
  is stuck on Stage 21 versus Stage 22 without reading any flag material.
- **`whistle_delay_us = 12000`.** Tuned so the timing step is roughly 3× the
  loopback HTTP noise floor; at 3 ms adjacent candidates could invert.
