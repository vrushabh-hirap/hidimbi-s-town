# Development.md — Build Specification & Implementation Guide

## Purpose of this document

This document is the authoritative implementation blueprint: exact files, components, data structures, build commands, and QA verification steps. It translates the design philosophy in `Idea.md` into concrete, reproducible code.

---

## Tech Stack & Architecture

- **Android Client:** Kotlin (minSdk 28, targetSdk 34) + Native C++ (Android NDK 26+, CMake).
- **Native Security Engine (`libtown.so`):** anti-debugging, memory checksumming, custom binary parsing, HKDF key derivation, and the four under-town primitives (Scriptorium VM, Drum Tower, Courier's Ledger, Granary race).
- **Client Storage Engine:** custom binary format (`assets/cave_store.bin`) replacing SQLite.
- **Steganographic Asset:** `assets/mosaic_wells.bin`, generated from `config.json` plus the Stage-12 attestation.
- **App Backend & Flag Service:** Python 3.11+ (FastAPI, Uvicorn, PyCryptodome, PyJWT, psycopg2) containerized via Docker.
- **External Datastore:** PostgreSQL 15 in its own container. **This is the only place the real flag exists.**

### Container topology

```
                 ┌──────────────────────────┐
  players ──────▶│ hidimbi_app   :8000      │  Lake API + Observatory + Courier
                 └───────────┬──────────────┘
                             │  (internal network only)
                             ▼
                 ┌──────────────────────────┐
                 │ hidimbi_flagger           │  :8001 public  /flag/{id}, /flag/claim
                 │                          │  :8100 internal /internal/gate
                 └───────────┬──────────────┘
                             │  parameterised SQL, role vault_flagger
                             ▼
                 ┌──────────────────────────┐
                 │ hidimbi_vault   :5432     │  postgres:15-alpine, flag lives here
                 └──────────────────────────┘
                 ┌──────────────────────────┐
                 │ hidimbi_flagger :8100      │  rotator (role vault_rotator)
                 └──────────────────────────┘
```

`:8100` is deliberately **not** published to the host. The only way to reach it is
the Stage-21 SSRF through `POST /api/v1/lake/courier`, plus the shared
`BACKEND_BEARER_SECRET` that only the app backend container holds.

---

## Author-Side Project Structure

```
hidimbis-town/
├── config.json                     # chain material (keys, nonces, decoys) — NO FLAG
├── app/                            # Android App Module
│   ├── src/main/
│   │   ├── AndroidManifest.xml
│   │   ├── cpp/                    # Native C++ Source (libtown.so)
│   │   │   ├── CMakeLists.txt
│   │   │   ├── native-lib.cpp      # anti-analysis, parsers, crypto, under-town JNI
│   │   │   ├── anti_analysis.h
│   │   │   ├── cave_parser.h       # Stage 5 TLV OOB leak
│   │   │   ├── town_crypto.h       # shared SHA-256 / HMAC / hex helpers
│   │   │   ├── scriptorium_vm.h    # Stage 14 VM
│   │   │   ├── drum_tower.h        # Stage 15 format string
│   │   │   ├── courier_ledger.h    # Stage 16 protobuf abuse
│   │   │   └── granary_race.h      # Stage 17 TOCTOU
│   │   ├── assets/
│   │   │   ├── cave_store.bin      # generated binary storage (replaces SQLite)
│   │   │   └── mosaic_wells.bin    # Stage 13 steganographic mosaic
│   │   └── java/com/vrushabhhirap/hidimbistown/
│   │       ├── MainActivity.kt
│   │       ├── TownProvider.kt     # Forest & Cave ContentProvider
│   │       ├── TownReceiver.kt     # Temple BroadcastReceiver
│   │       ├── TownService.kt      # Palace Bound Service (AIDL IPC)
│   │       ├── TownActivity.kt     # Market Activity (Parcelable gadget)
│   │       ├── model/TradeOffer.kt
│   │       └── native/NativeBridge.kt   # JNI declarations, stages 11–17
│   └── build.gradle.kts
├── backend/
│   ├── Dockerfile
│   ├── entrypoint.sh
│   ├── rotator_entrypoint.sh
│   ├── docker-compose.yml           # 4 services: app, flagger, vault, rotator
│   ├── requirements.txt
│   ├── server.py                    # Lake API + Observatory + Whistle + Courier
│   ├── flag_service.py              # ticket gate, internal index, /flag/claim
│   ├── db/
│   │   └── init/
│   │       ├── 01_schema.sql        # tables, functions, RLS
│   │       ├── 02_roles.sql         # least-privilege grants
│   │       └── 10_provision.sh      # container-side provisioning
│   └── db_access.py                 # shared connection + fail-closed helpers
├── scripts/
│   ├── dev_stack.sh                 # 4-process local runner (no Docker needed)
│   └── rotate_flag.py
├── tools/
│   ├── gen_cave_store.py            # config.json -> cave_store.bin
│   ├── gen_mosaic.py                # attestation + config.json -> mosaic_wells.bin
│   ├── gen_keys.py                  # RSA keypair for RS256 tickets
│   └── host_harness/
│       └── harness.cpp              # host build of the under-town C++ (QA)
└── solve_scripts/
    └── solve_hidimbi.py             # end-to-end solver, all 22 stages
```

---

## Central Configuration (`config.json`)

```json
{
  "challenge_name": "Hidimbi's Town",
  "version": "3.0.0",
  "flag_format": "KCTF{HIDIMBI_TOWN_%s}",
  "keys":   { "k1": "...", "k2": "...", "k3": "...", "k4": "..." },
  "nonces": { "n1": "...", "n2": "...", "n3": "...", "n4": "..." },
  "decoys": { "decoy_1": "FLAG{...}", "decoy_2": "SETU{...}",
              "decoy_3": "SETU{...}", "decoy_4": "KCTF{DECOY...}" },
  "graph":   { "forest": 17, "cave": 29, "temple": 4, "palace": 8,
               "market": 13, "lake": 31, "...": "..." },
  "network": { "backend_port": 8000, "flag_service_port": 8001,
               "flag_service_internal_port": 8100, "db_port": 5432,
               "rate_limit_per_minute": 5, "replay_window_seconds": 60 },
  "under_city": {
    "mosaic_tiles": 9, "mosaic_tile_bytes": 256, "mosaic_payload_per_tile": 64,
    "mosaic_magic": "X9WELLS", "mosaic_salt": "...",
    "scriptorium_seed": "<32B hex>",
    "drum_marker_word": "0xD1B1D1B1", "drum_window_slots": 12,
    "drum_key": "<32B hex>", "scribe_rune": "<16B hex>",
    "courier_config_tag": "<16B hex>", "courier_echo_cap": 255,
    "granary_prefs_epoch_offset": 64, "granary_epochs": ["...", "...", "..."],
    "granary_secret": "<16B hex>", "granary_race_window_us": 6000,
    "curve_nonce_seed": "<32B hex>",
    "whistle_delay_us": 12000, "whistle_key": "<8B hex>"
  },
  "vault": { "instance_id": "hidimbi-main", "schema": "vault",
             "epoch_id": 1, "flag_ttl_seconds": 21600,
             "flag_rotation_seconds": 3600 }
}
```

> **There is no `flag_default_suffix` any more.** The suffix is generated inside
> PostgreSQL by `vault.provision()`.

---

## The Vault Datastore

### Why PostgreSQL over Redis

The flag row needs: a composite unique key `(instance_id, team_id, epoch_id)`,
transactional one-time consumption, `SELECT … FOR UPDATE` row locking so two
concurrent claims cannot both win, a TTL column, and a rotation path that retires
an epoch. PostgreSQL delivers all of it inside a single atomic
`vault.claim_by_selector()` call. Redis has no row locks and no secondary
uniques, and putting expiry logic in Lua would move rules out of the database.
PostgreSQL also lets the flagger hold a role with `EXECUTE` on exactly one
function and no `SELECT` on any table — with RLS enabled and no policy, the
`flag_value` column is literally unreachable except through the claim path.

### Schema & functions

`backend/db/init/01_schema.sql` defines:

| Object | Purpose |
| :--- | :--- |
| `vault.flag_instances` | the flag row; PK `(instance_id, team_id, epoch_id)` |
| `vault.flag_instances_live_unique` | one live row per team |
| `vault.flag_redemptions` | immutable one-time ledger |
| `vault.flag_rejections` | audit of failed claims |
| `vault.provision(instance, team, salt, epoch, ttl)` | mints a row (SQL-side CSPRNG suffix) |
| `vault.provision_with_selector(...)` | same, with a caller-supplied selector |
| `vault.rotate(instance, team, ttl)` | retires the live epoch, inserts a new one |
| `vault.stub_for_team(instance, team)` | Stage-12 metadata only, never `flag_value` |
| `vault.log_rejection(...)` | audit row insert |
| `vault.claim_by_selector(instance, team, selector, ip)` | **the Stage-22 read** |

`backend/db/init/02_roles.sql` creates `vault_owner`, `vault_flagger` and
`vault_rotator` and revokes everything except the specific functions above.

### Provisioning

`backend/db/init/10_provision.sh` runs as the vault container's initdb hook. It
creates the two runtime roles with the platform-supplied passwords, calls
`vault.provision()`, enables RLS on `flag_instances`, and prints a manifest. The
manifest is the only place the plaintext flag ever lands, and it goes to stdout
(the platform's provisioning hook) — never into a file in the repository.

`scripts/dev_stack.sh` (the local, Docker-free runner) does the same thing in a
single `psql` call so the whole stack can be brought up on a laptop.

---

## Under-Town Primitives (`libtown.so`)

All four live in header files that are compiled both into `libtown.so` (NDK, ARM64)
and into `tools/host_harness/harness.cpp` (host, for QA). The host harness lets
`solve_hidimbi.py` verify the *actual shipped C++* without an Android device.

| Header | Stage | Entry point | Vulnerability |
| :--- | :--- | :--- | :--- |
| `scriptorium_vm.h` | 14 | `scriptorium_run()` | 32-bit VM, 25 opcodes, gate word |
| `drum_tower.h` | 15 | `drum_tower_log()` | user string used as a `printf` format |
| `courier_ledger.h` | 16 | `courier_parse_scroll()` | Group-wire-type confusion → unclamped echo |
| `granary_race.h` | 17 | `granary_query()` | double read of an mmap'd page across a yield |

`town_crypto.h` holds the shared SHA-256 / HMAC / hex helpers so all four and
`native-lib.cpp` agree bit-for-bit.

### The Scriptorium ISA (Stage 14)

```
0x00 NOP             0x01 PUSH imm32      0x02 POP r
0x10 LOAD r, off32   0x11 MOV rd, ra      0x12..0x19 ALU
0x1A..0x1D SHL/SHR/ROL/ROR rd, ra, imm
0x20 CMPEQ / 0x21 CMPLT rd, ra, rb
0x30 JMP rel16       0x31 JZ r, rel16     0x32 JNZ r, rel16
0x40 UNLOCK          (traps unless R0 == gate_word)
0xFF HALT            (publishes R0..R3 as the 16-byte mark)
```

8 registers `R0..R7`, a 32-entry stack, signed 16-bit relative branches, and a
2 000 000-instruction step cap. `UNLOCK` is the first instruction any working
program must issue, and it only passes once `R0` holds `LE32(seed[0:4])`.

### The Drum Tower (Stage 15)

`drum_tower_log()` hands the caller-supplied scroll straight to a format
interpreter. Positional parameters `%n$x` resolve against a 12-word "resonance
window" in `.bss`:

```
slot 1        : 0xD1B1D1B1  (the marker a solver uses to find the window)
slot 2 .. 9   : the 32-byte drum key, little-endian words
slot 10 .. 12 : padding
```

The tower first demands `HMAC-SHA256(K1‖K2, scriptorium_mark)`, so it will not
speak before Stage 14 is finished.

### The Courier's Ledger (Stage 16)

Hand-rolled protobuf reader. Field 3 may be encoded either length-delimited
(wire 2) or as a Group (wire 3). The Group path takes the group's total content
length as the fragment length and stores it into `g_bag.echo_len` **unclamped**,
while copying only the first 64 bytes into `g_bag.echo`. The reply then emits
`hex(g_bag.echo[0 : echo_len])`, so an unclosed group pushes `echo_len` past 64
and reads into the adjacent `scribe_rune` and `config_tag`.

### The Granary (Stage 17)

`granary_query()` reads the epoch from an mmap'd page, validates the pilgrim
rune against it, yields for `granary_race_window_us`, then re-reads the same
page. If the two reads differ, the granary releases its secret. On Android the
page is a `FileChannel.map()` view of the package's own
`shared_prefs/granary_ledger.xml`; the host harness `mmap()`s a real file so the
race is genuine on both platforms.

---

## Backend Services (`backend/`)

### `server.py` — Lake API, Observatory, Whistle, Courier

| Endpoint | Stage | Notes |
| :--- | :--- | :--- |
| `POST /api/v1/lake/trade` | 9/10 | unchanged WAF + master HMAC |
| `GET  /api/v1/observatory/jwks.json` | 18 | publishes `{kty,n,e,kid}` |
| `POST /api/v1/observatory/login` | 18 | accepts any `alg`; for HS256 it keys HMAC from the **JWK modulus** |
| `POST /api/v1/observatory/ecdh` | 19 | unclamped RFC 7748 Montgomery ladder, no point validation |
| `POST /api/v1/observatory/whistle` | 20 | 12 ms sleep per matched leading byte; returns `matched_bytes` |
| `POST /api/v1/lake/courier` | 21 | SSRF; only `flagger`/`vault` hostnames, forwards to `:8100` with the shared bearer |

### `flag_service.py` — Flag Service

| Endpoint | Port | Notes |
| :--- | :--- | :--- |
| `GET  /healthz` | both | liveness only |
| `GET  /flag/{flag_id}` | 8001 | Stage 12; RS256 gate + `stub_for_team` descriptor |
| `POST /internal/gate` | 8100 | Stage 21 target; requires the shared backend bearer |
| `POST /flag/claim` | 8001 | Stage 22; proof + seal + selector, then the live SQL read |

---

## Build, QA & Deployment Commands

### 1. Generating configuration & assets

```bash
# 1. RSA keypair for the RS256 tickets
python3 tools/gen_keys.py

# 2. cave_store.bin from config.json (replaces SQLite)
python3 tools/gen_cave_store.py config.json app/src/main/assets/cave_store.bin

# 3. the Stage-13 mosaic. The third argument is the Stage-12 attestation the
#    flagger mints; regenerate it whenever the attestation changes.
python3 tools/gen_mosaic.py config.json app/src/main/assets/mosaic_wells.bin \
    "HIDIMBI_ATTESTATION_UNSEALED"

# 4. the host harness used by the solver's stages 14-17
clang++ -std=c++17 -O1 -o tools/host_harness/harness \
    tools/host_harness/harness.cpp

# 5. build the APK
./gradlew assembleRelease
```

### 2. Deploying the container stack

```bash
cd backend
# the platform supplies these; none of them is committed
export HIDIMBI_INSTANCE_ID=hidimbi-main
export HIDIMBI_TEAM_ID=solo
export HIDIMBI_FLAG_SALT=$(openssl rand -hex 16)
export DB_PASSWORD=$(openssl rand -hex 32)
export ROTATOR_PASSWORD=$(openssl rand -hex 32)
export BACKEND_BEARER_SECRET=$(openssl rand -hex 32)
export FLAGGER_PROOF_KEY=$(openssl rand -hex 32)
export VAULT_ADMIN_PASSWORD=$(openssl rand -hex 32)
docker compose up -d --build
```

### 3. Player-facing attack surface (what the app actually exposes)

`MainActivity` is *not* a game. It renders a static list of the fourteen
landmarks in the **decoy** order plus a line of flavour text, and does nothing
else. That is deliberate: the contact surface is the four exported components
and the native library, not the UI. The manifest exports exactly:

| Component | Path / action | Used by |
| :--- | :--- | :--- |
| `MainActivity` | `MAIN`/`LAUNCHER` | anti-analysis init only (stages 1–2) |
| `TownProvider` | `content://…/forest`, `/cave`, `/mosaic`, `/scriptorium`, `/drum`, `/courier`, `/granary`, `/granary/advance` | stages 3, 5, 13, 14, 15, 16, 17 |
| `TownReceiver` | `…TEMPLE_PILGRIMAGE` | stage 6 |
| `TownService` | bound service, transaction `0x00000001` | stage 7 |
| `TownActivity` | `…MARKET_TRADE`, `…LAKE_REFLECTION` | stages 8 |

`/mosaic`, `/scriptorium`, `/drum`, `/courier` and `/granary` exist so the
under-town natives can be driven with plain `adb`. They are convenience
entry points onto exports that are equally reachable by writing your own caller
into `libtown.so`; a solver is not forced through `adb`.

### 4. Local, Docker-free runner

`scripts/dev_stack.sh` stands up a real PostgreSQL on its own port plus the app
backend, flag service and internal index as three separate OS processes, then
provisions the vault:

```bash
./scripts/dev_stack.sh up       # start everything + provision the vault
./scripts/dev_stack.sh solve    # run the full 22-stage solver
./scripts/dev_stack.sh rotate    # exercise vault.rotate()
./scripts/dev_stack.sh down      # stop everything
```

### 5. Automated QA verification

```bash
./scripts/dev_stack.sh up && ./scripts/dev_stack.sh solve
# or, pointed at the deployed containers:
python3 solve_scripts/solve_hidimbi.py \
    --backend http://localhost:8000 \
    --flag-service http://localhost:8001 \
    --keeper http://localhost:8100 \
    --instance hidimbi-main --team solo
```

Expected final line:

```
[SUCCESS] CHALLENGE FULLY SOLVED: KCTF{HIDIMBI_TOWN_XXXXXXXXXXXX}
```

---

## Regression Safety Net

Before claiming a change is safe, run:

```bash
# 1. native code still compiles for ARM64
ndk/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android*-clang++ \
    -fsyntax-only -std=c++17 \
    -I app/src/main/cpp app/src/main/cpp/native-lib.cpp

# 2. the host harness still agrees with the shipped C++
clang++ -std=c++17 -O1 -o tools/host_harness/harness tools/host_harness/harness.cpp
python3 solve_scripts/solve_hidimbi.py --backend ... --flag-service ... --keeper ...

# 3. the flag service fails closed
#    stop PostgreSQL, re-run the solver's stage 22, expect 503 and no flag

# 4. one-time semantics hold
#    re-run the solver, expect a refusal on the same selector
```

---

## Architecture & Thinking Notes (Chat Decisions & Rationale)

- **Separate containers for the flag service and the datastore** were non-negotiable. When the flag service and the database shared a container, a single `docker exec` reached both. Now the flagger needs a database password injected at deploy time, and the password is only ever in the process environment.
- **`:8100` is never published.** That is what makes Stage 21's SSRF mandatory rather than decorative: the internal index is only reachable from the app backend's network namespace.
- **`BACKEND_BEARER_SECRET` is a second key on the SSRF.** Network position alone is not treated as authentication; a direct request to the internal port fails even from another container.
- **The epoch is resolved from the vault, never from config.** A rotation therefore cannot desynchronise the flagger's seal check — this was a real bug during development, where a rotated row kept an epoch-1 selector while the solver derived an epoch-2 one.
- **The selector deliberately excludes the epoch.** The chain material defines the player's identity, not the administrative epoch; the epoch only decides *which row* they claim.
- **`whistle_delay_us = 12000`** was chosen by measuring: 3 ms inverted adjacent candidates on loopback, 12 ms is ~3× the noise floor and makes the oracle deterministic.
- **The host harness exists to protect stages 1–12.** All the under-town primitives are compiled from the same headers that ship in `libtown.so`, so a change that would silently break Stage 14 is caught by `solve_hidimbi.py` on a laptop with no device attached.
