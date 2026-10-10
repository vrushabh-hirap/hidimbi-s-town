# Description.md — Participant-Facing Challenge Briefs

## Purpose of this document

This file contains the copy shown to participants on the competition platform: challenge title, category, difficulty, story blurb, and the files/access provided. This is the only file in this set that is safe to show to participants as-is (after filling in event-specific details like points value and download links). Everything else in this set (`Idea.md`, `Development.md`, `Flags.md`, `Hints.md`) is internal-only.

---

## Challenge — Hidimbi's Town

**Category:** Android Exploitation / Native Reverse Engineering (JNI/ARM64) / IPC Security / Cryptography / Binary Parsing & VM Reversing / Web & API Exploitation / Side Channels / Database Security
**Difficulty:** Insane / Expert (Grandmaster Tier)
**Points:** `[TBD — set per event scoring scale, recommended 500 / Max Tier]`

### Story

*Deep in the enchanted forest, Hidimbi built a sanctuary so unassuming that no traveler thought to look twice — a town of ancient temples, secluded caves, grand palaces, and a quiet lake, hiding in plain sight among ordinary villagers who never suspected what powers walked among them.*

*You hold a weathered map of that town. It looks like an innocent game. Six landmarks to explore, nothing more. But every road in this town is an intricate lock, and some of those roads were never meant to exist — let alone be traversed in the sequence you'll have to uncover. The visual map lies. The villagers feed you illusions. Only by subverting the low-level fabric of the town, piercing native deception routines, unearthing hidden IPC channels, and chaining multi-stage cryptographic proofs will the true path emerge.*

*What the map does not show is that the town has a floor beneath it. Beneath the market square there is a mosaic of nine wells, a scriptorium that speaks a language of its own, a drum tower that still runs an old logging routine, a courier's desk with a parser that never learned the new protocol, a granary ledger kept in shared memory, an observatory that publishes its keys, a relay that answers slowly, and a vault. The vault is not in this town at all. It is somewhere else entirely, on the other side of the river, and it will not open for anything but the key you assemble from everything above it.*

### What you're given

- `Dimbapura.apk` — the target Android application.
- `[Backend API URL — TBD, fill in once hosted]`
- `[Flag Service URL — TBD, fill in once hosted]`
- No source code, no database dumps, and no further credentials are provided. The target APK, the remote WAF-hardened backend, and the isolated Flag Service constitute the entire attack surface.

### The chain, at a glance

Twenty-two stages. The first twelve are the pilgrimage above ground — Forest,
Cave, Temple, Palace, Market, Lake. The last ten are the town underneath, and
not one of them can be started without the one before it:

| # | Landmark | What you will need |
| :-- | :--- | :--- |
| 13 | Mosaic of the Nine Wells | the Stage-12 attestation |
| 14 | The Scriptorium | the mosaic seed |
| 15 | The Drum Tower | the scriptorium mark |
| 16 | The Courier's Ledger | the drum key |
| 17 | The Granary | the scribe rune |
| 18 | The Observatory | the granary secret |
| 19 | The Astronomer's Curve | an astronomer's token |
| 20 | The Keeper's Whistle | the handshake |
| 21 | The Courier's Errand | the whistle key |
| 22 | The Vault | the selector you assemble from all of the above |

### Flag format

`KCTF{...}`
(Example: `KCTF{HIDIMBI_TOWN_7E9B4F1A2C3D}`)

### A note before you start

Attacking any single part of this system in isolation — whether decompiling the Java bytecode, dumping local storage, probing the API, sniffing IPC traffic, or reaching the final service — will yield fragmented breadcrumbs or trap you in elaborate decoys. Every artifact you recover belongs to a strict, interconnected cryptographic chain.

**The app has no game in it.** Opening it shows a list of fourteen landmark names and a map that lies. There is nothing to play. That is not a broken install and there is no hidden button — the whole challenge lives in the *components* behind those names, in the native library, and in the services on the other side of the river. Decompile first.

The flag itself is not on the machine anywhere you can reach directly. It is a row in a database that lives on its own, and the only thing that authorises reading that row is the key you derive by completing the chain. There is no admin key, no back door, and no config file to read. If you stop halfway, you have nothing.

---

## How the surface is laid out (what you are poking at, per stage)

This table exists because it is genuinely hard to work out from the outside. It says *how* to touch each stage, not how to beat it.

| Stage | Surface | What you actually run |
| :-- | :--- | :--- |
| 1–2 | **static + runtime** | `jadx -d out Dimbapura.apk`; read `AndroidManifest.xml`; then open `libtown.so` in Ghidra/IDA. The app itself only initialises the native engine. |
| 3 | **adb → ContentProvider** | `adb shell content query --uri content://com.vrushabhhirap.hidimbistown.provider/forest --where "token=<hex>"` |
| 4 | **static** | arithmetic on the node/direction values stages 3, 5, 6, 7, 8 hand you |
| 5 | **adb → ContentProvider** | `content query --uri content://…/cave --where "offset=-16&len=64"` |
| 6 | **adb → BroadcastReceiver** | `adb shell am broadcast -a com.vrushabhhirap.hidimbistown.TEMPLE_PILGRIMAGE --es pilgrim_token <hex>` |
| 7 | **adb → bound Service** | bind `onBind`, craft a transaction `0x00000001` with the auth proof |
| 8 | **adb → Activity** | `am start -n …TownActivity -a com.vrushabhhirap.hidimbistown.MARKET_TRADE --es trade_item <parcel>` |
| 9–10 | **network client** | `POST /api/v1/lake/trade` with the WAF headers and the signed proof |
| 11 | **static + local compute** | HKDF over the recovered nonces; the KDF is in `libtown.so` if you want the ground truth |
| 12 | **network client** | `GET /flag/<flag_id>` with the bearer |
| 13 | **static (asset)** | `unzip -o Dimbapura.apk assets/mosaic_wells.bin` — or, if you prefer to stay inside the app, `adb shell content query --uri content://…/mosaic` returns the same 2304 bytes as hex |
| 14 | **adb → ContentProvider** | `content query --uri content://…/scriptorium --projection "" --where "program=<hex>&seed=<hex>&gate=<u32>"` — the provider runs your bytecode through `libtown.so` |
| 15 | **adb → ContentProvider** | `content query --uri …/drum --where "note=%1$x %2$x …&mark=<hex>&gate=<hex>"` |
| 16 | **adb → ContentProvider** | `content query --uri …/courier --where "scroll=<base64 protobuf>&key=<drum key hex>"` |
| 17 | **adb → ContentProvider (race)** | `content query --uri …/granary --where "rune=<hex>&scribe=<hex>&secret=<hex>&epoch=<u64>"` while, from a second shell, `content insert --uri …/granary/advance --where "epoch=<u64>"` rewrites the same mapped page |
| 18–20 | **network client** | Observatory JWKS / login / ECDH / whistle |
| 21 | **network client** | the Lake courier, used as an SSRF into the flag service's internal index |
| 22 | **network client** | `POST /flag/claim` on the flag service, which does the database read |

The under-town primitives (stages 14–17) are native calls inside `libtown.so`. The
app exposes them through `content://` so you can drive them with `adb`; if you
would rather call them yourself you will find the exports
`Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_runScriptorium`,
`…_drumTowerFormat`, `…_parseCourierScroll` and `…_granaryQuery` in the stripped
`.so`, and a replay harness for them is a twenty-line C program.

---

## Shared submission notes (for the scoring platform)

- The challenge accepts exactly one genuine flag in `KCTF{...}` format per team/instance.
- The challenge contains multiple decoy strings (`FLAG{...}`, `SETU{...}`) designed to mimic flags. Submitting decoys will score zero and alert monitoring. This is an intentional defense mechanism.
- The real flag is dynamically issued by the flag service only after a live, verified database read authorised by the completed exploit chain.
- Hints are available via the ladder in `Hints.md` if teams become trapped in anti-analysis or graph misdirections.

---

## Architecture & Thinking Notes (Chat Decisions & Rationale)

- **Flag Format Standard:** the flag format is strictly `KCTF{HIDIMBI_TOWN_<12 chars>}` across all scoring and retrieval endpoints.
- **External Datastore, Not a Config File:** the participant-facing copy deliberately does not mention PostgreSQL or a schema — the point is that the flag is "somewhere else entirely", which is exactly how the challenge behaves.
- **Difficulty Calibration:** tuned to **Insane / Expert**. Solvers must overcome native JNI anti-debugging, memory integrity checks, binder IPC reversing, custom binary store memory corruption, parcelable deserialization vulnerabilities, WAF device fingerprinting, native HKDF/ChaCha20 key derivation, LSB steganography, custom VM bytecode interpretation, format-string exploitation, protobuf wire-format abuse, mmap TOCTOU races, JWT algorithm confusion, small-subgroup elliptic curve attacks, differential timing side channels, SSRF against an internal service, and finally an authorised live database read.
- **No Shortcuts Anywhere:** every remaining credential — the vault password, the flagger proof key, the backend bearer — is injected by the platform at deploy time and is never part of the challenge.
