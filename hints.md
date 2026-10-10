# Hints.md — Progressive Hint Ladder

## Purpose of this document

This document specifies the progressive hint ladder for the Hidimbi's Town challenge. Hints are structured across three escalating tiers to support stuck teams without spoiling key insights or compromising the competition's competitive integrity.

---

## Hint Structure & Cost Matrix

- **Tier 1 (Mindset / Strategic Orientation):** clarifies the conceptual framing required for the stage. Low cost / early unlock.
- **Tier 2 (Tooling / Technique):** highlights the relevant technique. Medium cost.
- **Tier 3 (Tactical Guidance):** near-explicit instruction. High cost / late unlock.

Stages 1–12 are the pilgrimage above ground. Stages 13–22 are the town
beneath it: none of them can be reached before Stage 12.

---

## Under-Town Stages 13–22

> **Reading this section.** Every stage below is driven from *outside* the app:
> `adb` against an exported component, `unzip` on the APK, or an HTTP client
> against the Lake API. There is no in-app button that walks the chain for you.
> The surface used by each stage is named in the hint's Tier 2/3 line, and the
> full table of surfaces is in `description.md`.

### Stage 13: The Mosaic of the Nine Wells
- **Tier 1:** "The Keeper gave you a scroll. Scrolls in this town are written on stone."
- **Tier 2:** "`assets/mosaic_wells.bin` is 9 tiles of 256 bytes. `unzip` it out of the APK (or `content query --uri content://…/mosaic`). Count the bits that are not stone."
- **Tier 3:** "Four stone bytes carry one payload byte, packed from the end of each tile backwards. The tile order and the payload mask are both keyed by the Stage-12 attestation string — without it you get noise. Verify with the `X9WELLS` magic and the chain-master HMAC tag."

### Stage 14: The Scriptorium
- **Tier 1:** "The Scriptorium answers in a language of its own."
- **Tier 2:** "`NativeBridge.runScriptorium` (export `…_runScriptorium` in `libtown.so`) takes a byte array. `adb shell content query --uri content://…/scriptorium --where "program=<hex>&seed=<hex>&gate=<u32>"` drives it; or write your own caller. Write an assembler — the ISA is in `development.md`. `UNLOCK` traps unless `R0` already holds the first word of the seed."
- **Tier 3:** "Keep `R0..R3` for results and use `R4..R7` as scratch, or later rounds will clobber you. A shared modpow loop with `PUSH 2 / POP / MOD` for the low-bit test is enough to compute all four marks."

### Stage 15: The Drum Tower
- **Tier 1:** "The drums still ring from the last pilgrim who wrote on them."
- **Tier 2:** "`adb shell content query --uri content://…/drum --where "note=%1$x %2$x …&mark=<hex>&gate=<hex>"`. The tower logs your scroll through a format interpreter. Look at `%n$x`."
- **Tier 3:** "`%1$x` prints the marker `0xD1B1D1B1`; `%2$x` … `%9$x` are the drum key words. The gate tag is `HMAC-SHA256(K1‖K2, scriptorium_mark)`."

### Stage 16: The Courier's Ledger
- **Tier 1:** "Old couriers sealed their scrolls with knots, not brackets."
- **Tier 2:** "`adb shell content query --uri content://…/courier --where "scroll=<base64>&key=<drum key hex>"`. The ledger's reader (exported `…_parseCourierScroll`) accepts a deprecated *Group* (wire type 3) where a nested message belongs; the group's length is stored unclamped."
- **Tier 3:** "Send field 1 as the SHA-256-derived seal over the drum key, then field 3 with wire type 3 and ~100 bytes of body and no EndGroup. The reply's `echo_len` exceeds 64 and reads past `echo` into `scribe_rune` at offset 68."

### Stage 17: The Granary
- **Tier 1:** "The granary ledger is a page of memory, and memory is shared."
- **Tier 2:** "`adb shell content query --uri content://…/granary --where "rune=<hex>&scribe=<hex>&secret=<hex>&epoch=<u64>"` from one shell while a second shell runs `content insert --uri …/granary/advance --where "epoch=<u64>"` — both map the same 4096-byte page, which is the race."
- **Tier 3:** "Present `HMAC-SHA256(scribe_rune, "GRANARY|" + hex(epoch))` for the current epoch, and change the epoch in the mapped page inside the 6 ms window. The two reads disagree and the secret is released."

### Stage 18: The Observatory
- **Tier 1:** "The astronomers publish their keys so others can verify them."
- **Tier 2:** "`/api/v1/observatory/jwks.json` hands out the key. The Observatory's verifier trusts the `alg` in your token, and for HS256 it keys the HMAC with a *public* string."
- **Tier 3:** "Fetch the JWK, take `n` (a base64url string), and sign `alg: HS256` with it as the HMAC secret. PyJWT will refuse to do this for you — build the token yourself."

### Stage 19: The Astronomer's Curve
- **Tier 1:** "An orrery with an unclamped gear is only as strong as its smallest wheel."
- **Tier 2:** "The ladder performs no point validation and never clamps the scalar. Some points collapse the whole multiplication into a tiny set."
- **Tier 3:** "Submit `u = 325606250916557431795983626356110631294008115727848805560023387167927233504` (a point of order 4 on the twist). The shared secret is one of `{0, 1}`; the returned handshake tells you which. Confirm your guess locally with `HMAC(observatory_salt, hex(shared))`."

### Stage 20: The Keeper's Whistle
- **Tier 1:** "The whistle does not answer what you are, only what you are not."
- **Tier 2:** "Measure `elapsed_us` on the relay. It grows by 12 000 µs for each correctly matched leading byte."
- **Tier 3:** "Rank the 256 candidates per position by response time, then confirm with the relay's own `matched_bytes` counter before committing a byte. When you submit the whole key the relay answers `THE_WHISTLE_ANSWERS` and hands it to you."

### Stage 21: The Courier's Errand
- **Tier 1:** "The Lake's courier will carry a letter for you — to internal addresses."
- **Tier 2:** "`POST /api/v1/lake/courier` fetches a URL on the pilgrim's behalf. Only internal hostnames are allowed."
- **Tier 3:** "Point it at `http://flagger:8100/internal/gate` with your selector and proof. The reply is the one-time `seal` the final stage demands. The courier also needs the shared bearer, which it carries itself."

### Stage 22: The Vault Below
- **Tier 1:** "Under the town there is a ledger, and it is not written for you."
- **Tier 2:** "The flag is a row in a live database, and the read is parameterised by a 32-byte selector you derive from the whole chain."
- **Tier 3:** "`selector = SHA-256("HIDIMBI_TOWN_SELECTOR_v1" ‖ K1..K4 ‖ N1..N4 ‖ seed ‖ drum_key ‖ scribe_rune ‖ granary_secret ‖ whistle_key ‖ instance:team)`. Submit `{selector, proof, seal}` to `POST /flag/claim` with `X-Team-Id` and `X-Epoch-Id`. Watch the failures: `503` means the datastore was unreachable and no flag was issued."

---

## Pre-Under-Town Stages 1–12 (abridged; see prior revisions for full text)

### Stage 1–2: Recon, Native Defense & Deception
- **Tier 1:** "The town map looks like a simple game, but the true defenses sleep beneath the bytecode."
- **Tier 2:** "Inspect `libtown.so`. Why does the app behave differently under an emulator or with Frida attached?"
- **Tier 3:** "The native engine spawns watchdog threads checking `ptrace` and `/proc/self/maps`. If detected it silently routes you to a decoy graph returning `SETU{...}` flags. Neutralise the checks or run on a clean physical device."

### Stage 3: Forest
- **Tier 1:** "The Forest provider is open, but it refuses to speak to strangers."
- **Tier 2:** "`X-Town-Token` is derived from the app's own cryptographic identity."
- **Tier 3:** "Compute HMAC-SHA256 of the package signing certificate fingerprint, salted with `FLAG{HIDIMBI_IS_WATCHING}` from `strings.xml`. Pass it as `X-Town-Token` to unlock node 17."

### Stage 4: Graph Topology
- **Tier 1:** "The visual order shown on the town artwork is a deceptive mirage."
- **Tier 2:** "Each landmark returns both an ID and a direction byte. How do they combine?"
- **Tier 3:** "`Next Node = (Current ID + dir) % 37` gives Forest(17) → Cave(29) → Temple(4) → Palace(8) → Market(13) → Lake(31)."

### Stage 5: Cave
- **Tier 1:** "There is no relational database here. Look into the custom binary vault in the assets."
- **Tier 2:** "`/cave` invokes a native C++ TLV parser. What happens when record bounds checks handle signed values?"
- **Tier 3:** "The parser validates lengths with a signed comparison. Supplying a negative offset (`offset=-16&len=64`) triggers an out-of-bounds read that leaks K1 and N1. A naive query for `secret` only gives Decoy Flag 4."

### Stage 6: Temple
- **Tier 1:** "The Temple doors are guarded by a custom permission, but the guards have forgotten to check credentials properly."
- **Tier 3:** "Broadcast `com.vrushabhhirap.hidimbistown.TEMPLE_PILGRIMAGE` with an extra token set to `SHA-256(K1 ⊕ N1)`. The receiver replies with K2 and N2."

### Stage 7: Palace
- **Tier 1:** "The Palace service communicates over Binder without public interface stubs."
- **Tier 3:** "Send transaction `0x00000001` with `auth_proof = HMAC-SHA256(K1‖K2, N1‖N2)` and node `0x08`. It returns K3, N3 and Encrypted Blob E1."

### Stage 8: Market
- **Tier 1:** "In the Market, numbers can deceive if they grow larger than the container that holds them."
- **Tier 3:** "Craft a `TradeOffer` where `quantity * unitPrice` overflows a signed 32-bit int (100,000 × 30,000). The negative total bypasses the credit check, revealing K4, N4 and the Lake API URL."

### Stage 9–10: Lake API
- **Tier 1:** "The Lake API will reject requests that look like automated scripts."
- **Tier 3:** "Sign with the 64-byte key `K1‖K2‖K3‖K4`. The message is `31:<device_id>:<timestamp>:N1:N2:N3:N4`, the device fingerprint is `SHA-256(ANDROID_ID + K4)`, and the timestamp must be within ±60 s."

### Stage 11–12: Decryption & the Keeper's Index
- **Tier 1:** "The encrypted artifact is a ticket, not the flag itself."
- **Tier 3:** "`deriveFinalKey()` (or its HKDF-SHA256 equivalent: salt `K1‖K2`, IKM `K3‖K4`, info `N1..N4`) decrypts Blob E1 into the flag id and the RS256 flag-service token. `GET /flag/<flag_id>` with that bearer returns the attestation that keys Stage 13."

---

## Decoy Guidance (triggered only upon decoy submission)

- **Decoy 1 (`FLAG{HIDIMBI_IS_WATCHING}`):** "This string was placed in plain sight for a reason. It is a cryptographic ingredient for Stage 3, not the final flag."
- **Decoy 2 (`SETU{FOREST_PATH_COMPLETE}`):** "You triggered the native anti-analysis defense. The town fed you an illusion because it sensed your debugger or instrumentation hooks."
- **Decoy 3 (`SETU{LAKE_REFLECTION_COMPLETE}`):** "You touched the Lake surface without completing the pilgrimage across all prior realms."
- **Decoy 4 (`KCTF{DECOY_CAVE_ILLUSION_OF_MAYA}`):** "A standard database query only returns what the shadows want you to see. Exploit the native memory parser to reach the true secret."

---

## Architecture & Thinking Notes (Chat Decisions & Rationale)

- **Nothing in Stages 13–22 is reachable in isolation.** Each one begins from a value handed over by the previous stage: the attestation, the seed, the mark, the drum key, the scribe rune, the astronomer token, the whistle key, and finally the selector.
- **The hint ladder stops short of the selector formula for Stage 22** at Tier 2 by design: Stage 22 is the one stage whose "technique" is simply having done everything before it.
- **Decoy count unchanged.** Adding stages did not require new decoys, because the value of the under-town stages is dependency, not deception.
