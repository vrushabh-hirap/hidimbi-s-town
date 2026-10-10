# Idea.md — Concept & Design Philosophy

## Purpose of this document

This file defines *what* the challenge is, *why* each architectural decision was made, and the core philosophies governing its design. Anyone modifying or extending the challenge must read this document first so that added complexity aligns with the intended solver experience and operational goals.

---

## Core Design Philosophy

1. **Extreme / Insane Difficulty ("Grandmaster Tier"):** This challenge is explicitly calibrated to be among the most challenging Android reverse engineering and exploitation challenges in competitive CTFs. Solvers must demonstrate mastery across native ARM64 reversing, JNI internals, Linux kernel interfaces, Android IPC (Binder/AIDL), advanced component exploitation, memory corruption/parser vulnerabilities, custom cryptography, format-string and protobuf parsing abuse, custom virtual machines, race conditions, steganography, side channels, JWT/E​CC misconfiguration, SSRF, differential timing, and authenticated live database extraction.
2. **No Single Point of Total Compromise:** The flag cannot be extracted by inspecting any single artifact. It requires stitching together five independent domains: client static bytecode/native binary reversing, runtime IPC exploitation, custom binary format memory leakage, authenticated server-side cryptographic proofs, and a *live* read of an external datastore that only a completed exploit chain can authorize.
3. **Deceptive Anti-Analysis over Brittle Crashes:** Traditional anti-debugging that simply crashes (`exit(0)`) is trivial to locate and patch. In Hidimbi's Town, detection of emulators, root, Frida, or debuggers silently steers execution into a *shadow execution path*. This shadow path remains fully functional but yields plausible decoy flags (e.g. `SETU{FOREST_PATH_COMPLETE}`) and poisoned keys, leading solvers into elaborate wild-goose chases.
4. **The Flag Lives in the External Datastore:** The real flag is *only* a row in a PostgreSQL container that is separate from both the app backend container and the flag service container. It is never in the APK, in an image, in `config.json`, or in any file in this repository. Retrieving it requires a live, parameterised SQL read whose authorisation is the 32-byte **vault selector** derived by completing the whole chain.
5. **Theme as an Architectural Blueprint:** The Indian mythological tale of Hidimbi (the Rakshasi who lived disguised in an unassuming forest town) mirrors the challenge architecture: an innocent-looking town map masking dangerous, low-level attack surfaces. Six landmarks above ground, six more under it, and a vault beneath all of them.
6. **Per-Instance / Per-Team Isolation & Rotation:** Every instance and every team has its own flag row keyed by `(instance_id, team_id, epoch_id)`. Rotation retires the live epoch's row, so a leaked selector has a hard TTL, and no two deployments share anything.

---

## Challenge — Hidimbi's Town: Full Stage Architecture

**Tagline:** *"Every road in the town leads somewhere. Some roads were never meant to exist. The map lies."*

The original pilgrimage (Stages 1–12) takes the solver from the Forest to the Lake and to the Keeper's index at the edge of town. Stages 13–22 go **under** the town — the sewers, catacombs, catwalks and vaults the townsfolk never see — and are reachable only with material carried up from the pilgrimage.

Every new stage consumes something produced by an earlier one, and each introduces a technique found nowhere else in the chain.

### Stages 1–12 — the pilgrimage above ground

| # | Landmark | Technique | Requires from |
| :-- | :--- | :--- | :--- |
| 1 | Recon & manifest obfuscation | `PackageManager.setComponentEnabledSetting` | — |
| 2 | `libtown.so` anti-analysis | ptrace, `/proc/self/maps`, in-memory `.text` checksums | 1 |
| 3 | Forest — `TownProvider` | exported ContentProvider + `X-Town-Token` HMAC | 2 |
| 4 | Graph discovery | non-linear transition vector | 3 |
| 5 | Cave — native TLV store | signed-offset OOB read | 4 |
| 6 | Temple — `TownReceiver` | BroadcastReceiver permission confusion | 5 (K1, N1) |
| 7 | Palace — `TownService` | stripped AIDL / `onTransact` reversing | 6 (K2, N2) |
| 8 | Market — `TownActivity` | Parcelable + signed int32 overflow | 7 (K3, N3) |
| 9 | Lake API | WAF, device fingerprint, replay window | 8 (K4) |
| 10 | Cryptographic proof synthesis | 64-byte master HMAC | 5–8 |
| 11 | Native HKDF + AES-256-GCM | `deriveFinalKey` reconstruction | 10 |
| 12 | Keeper's index | RS256 ticket gate | 11 |

### Stages 13–22 — the town beneath the town

| # | Landmark | Technique (new to this chain) | Requires from |
| :-- | :--- | :--- | :--- |
| 13 | **Mosaic of the Nine Wells** | LSB steganography over a 9-tile mosaic whose tile permutation *and* payload mask are both keyed by the Stage-12 attestation | 12 |
| 14 | **The Scriptorium** | custom 32-bit VM bytecode — the solver must reverse a bespoke ISA and hand-write a modpow program for it | 13 (seed) |
| 15 | **The Drum Tower** | classic format-string bug; `%1$x…%9$x` walks the tower's implicit-argument window | 14 (mark) |
| 16 | **The Courier's Ledger** | protobuf wire-format abuse — the reader accepts a deprecated *Group* where a nested message belongs, and the resulting unclamped length reads past the ledger bag | 15 (drum key) |
| 17 | **The Granary** | TOCTOU on a memory-mapped shared preference; a racing writer changes the epoch inside the read-modify-read window | 16 (scribe rune) |
| 18 | **The Observatory** | JWT algorithm confusion — the verifier keys HS256 from the *published JWK modulus* | 17 |
| 19 | **The Astronomer's Curve** | small-subgroup confinement on an unclamped, unvalidated Montgomery ladder | 18 |
| 20 | **The Keeper's Whistle** | differential timing side channel (12 ms per matching leading byte) | 19 |
| 21 | **The Courier's Errand** | SSRF — the Lake courier is abused to reach the flagger's *internal* index | 20 + whole chain |
| 22 | **The Vault** | live, authorised read of the flag row from the external PostgreSQL datastore | 21 |

### Stage 22 in detail — the flag is a database row

```
                    ┌───────────────────────┐
   app backend  ───▶│  Lake API / Observatory│  (container: hidimbi_app)
   :8000            └──────────┬────────────┘
                               │  SSRF, Stage 21
                               ▼
   flag service    ┌───────────────────────┐
   :8001  ─────────▶│  Keeper's index :8100  │  (container: hidimbi_flagger,
                    │  public /flag/claim    │   internal port NOT published)
                    └──────────┬────────────┘
                               │  SELECT vault.claim_by_selector(...)
                               ▼
                    ┌───────────────────────┐
   PostgreSQL  ───▶│  vault.flag_instances  │  (container: hidimbi_vault,
   :5432            │  <- the flag lives here│   separate container)
                    └───────────────────────┘
```

The selector is:

```
selector = SHA-256(
    "HIDIMBI_TOWN_SELECTOR_v1"
    || K1||K2||K3||K4                  (Stages 5, 6, 7, 8)
    || N1||N2||N3||N4                  (Stages 5, 6, 7, 8)
    || scriptorium_seed                (Stage 13, steganography)
    || drum_key                        (Stage 15, format string)
    || scribe_rune                     (Stage 16, protobuf abuse)
    || granary_secret                  (Stage 17, TOCTOU race)
    || whistle_key                     (Stage 20, timing side channel)
    || "<instance_id>:<team_id>"
)
```

None of that material exists in the app, in an asset, or in any image: each
component has to be recovered by playing the challenge. The flagger recomputes
the selector itself and compares, then uses **the player's own derived bytes** as
the bound parameter of the SQL read, so the DB query is genuinely authorized by
the chain rather than by a credential the flagger holds.

There is no fallback: if PostgreSQL is unreachable, the flagger returns `503`
and no flag.

---

## Architecture & Thinking Notes (Chat Decisions & Rationale)

- **The Datastore Is PostgreSQL, Not Redis:** the flag row needs strict
  `(instance_id, team_id, epoch_id)` uniqueness, transactional one-time
  consumption, `FOR UPDATE` row locking so two concurrent claims cannot both
  succeed, a TTL, and a rotation path that retires an epoch. Redis would make
  those awkward (no native row locks, no secondary uniques) and its Lua scripts
  would put expiry logic outside the database. PostgreSQL gives all of it in one
  atomic `vault.claim_by_selector()` call, and lets the flagger hold a role with
  `EXECUTE` on exactly one function and no `SELECT` on any table — the flag
  literally cannot be read out of the database except through the claim path.
- **Why the Selector Is Server-Recomputed:** the flagger recomputing the
  expected selector looks like a shortcut, but it is not: the player still has
  to *produce* it, and the DP query is parameterised by the player's bytes. The
  recomputation exists only to reject guesses cheaply and to open a rejection
  row in the audit table.
- **The Two-Key Gate on Stage 22:** a valid selector is not enough. The player
  also needs the one-time `seal` minted by the flagger's internal index, which
  is reachable only through the Stage-21 SSRF. That makes the SSRF stage
  load-bearing rather than decorative: without it the vault will not answer.
- **Why `whistle_delay_us` = 12 ms:** at 3 ms the loopback HTTP noise (~2–5 ms)
  could invert two adjacent candidates, making the timing oracle unreliable.
  At 12 ms the step is roughly 3× the noise floor, and the whole 8-byte key is
  recovered deterministically.
- **Why the Mosaic Uses Both a Permutation and a Mask:** the tile permutation
  alone would still let a determined solver brute-force the 9! orderings by
  checking the magic. The per-attestation mask means even a correctly ordered
  payload is unreadable without the Stage-12 attestation string.
- **What Was Carried Over From the 12-Stage Version:** every original stage is
  preserved unchanged in behaviour, including all four decoys, the
  anti-analysis shadow path, and the original `KCTF{...}` flag format. The
  only deliberate change is the graph formula, which the code has always
  implemented as `(node + dir) % 37`; the documentation now matches the code.
