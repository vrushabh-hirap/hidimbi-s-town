#!/usr/bin/env python3
"""
tools/gen_mosaic.py
Compiles config.json + the Stage-12 flag service attestation into
`assets/mosaic_wells.bin`, the steganographic payload of Stage 13.

The Mosaic of the Nine Wells is a grid of "stone" tiles. Each tile is 256
bytes of pseudo-random stone whose two least significant bits per byte have
been overwritten with payload bits. Four stone bytes therefore carry exactly
one payload byte:

    2 bits/byte * 256 bytes = 512 bits = 64 payload bytes per tile
    9 tiles * 64 bytes       = 576 payload bytes in total

The *logical* order of the tiles is not their order in the file: it is a
permutation keyed by the Stage-12 attestation, so the payload cannot be
reassembled without the flag service's JWT.

Layout of the reassembled 576-byte payload:

    +0x000  magic   8 bytes   "X9WELLS\\0"
    +0x008  seed   32 bytes   the scriptorium seed
    +0x028  tag    16 bytes   HMAC-SHA256(K1||K2||K3||K4, seed)[:16]
    +0x038  filler 520 bytes  pseudo-random noise / decoy filler
"""

import sys
import os
import json
import hmac
import hashlib

TILE_BYTES = 256
BITS_PER_STONE_BYTE = 2
STONE_BYTES_PER_PAYLOAD_BYTE = 8 // BITS_PER_STONE_BYTE      # 4
PAYLOAD_PER_TILE = (TILE_BYTES * BITS_PER_STONE_BYTE) // 8    # 64


# ---------------------------------------------------------------------------
# A tiny xorshift PRNG. Deterministic from (salt, tile_index, offset).
# ---------------------------------------------------------------------------
def stone_byte(salt: bytes, tile_index: int, offset: int) -> int:
    state = int.from_bytes(salt[:8], "big") ^ (tile_index * 0x9E3779B97F4A7C15)
    state ^= offset * 0xC2B2AE3D27D4EB4F
    state &= 0xFFFFFFFFFFFFFFFF
    state ^= (state >> 12); state &= 0xFFFFFFFFFFFFFFFF
    state ^= (state << 25); state &= 0xFFFFFFFFFFFFFFFF
    state ^= (state >> 27); state &= 0xFFFFFFFFFFFFFFFF
    return (state * 0x2545F4914F6CDD1D) >> 56 & 0xFF


def tile_permutation(attestation: str, tiles: int):
    """Tile indices in payload order, keyed by the Stage-12 attestation."""
    key = hashlib.sha256(attestation.encode("utf-8")).digest()
    return sorted(range(tiles),
                  key=lambda i: hmac.new(key, str(i).encode(), hashlib.sha256).digest())


def build_mosaic(config, attestation: str):
    """Return (mosaic_bytes, tile_order) where tile_order[slot] is the file
    index that carries payload slot `slot`."""
    uc = config["under_city"]
    tiles = uc["mosaic_tiles"]
    salt = bytes.fromhex(uc["mosaic_salt"])
    magic = uc["mosaic_magic"].encode("ascii")
    assert len(magic) == 7, "mosaic magic is 7 ASCII bytes + one NUL"

    k1 = bytes.fromhex(config["keys"]["k1"])
    k2 = bytes.fromhex(config["keys"]["k2"])
    k3 = bytes.fromhex(config["keys"]["k3"])
    k4 = bytes.fromhex(config["keys"]["k4"])
    chain_master = k1 + k2 + k3 + k4

    seed = bytes.fromhex(uc["scriptorium_seed"])
    tag = hmac.new(chain_master, seed, hashlib.sha256).digest()[:16]

    # The seed is additionally masked with a keystream derived from the
    # Stage-12 attestation, so even reading the right permutation is not
    # enough: the pilgrim must actually hold the flag service's JWT.
    attest_key = hashlib.sha256(
        attestation.encode("utf-8") + b"|MOSAIC_V1").digest()
    keystream = (attest_key + hashlib.sha256(attest_key).digest())[:32]
    masked_seed = bytes(a ^ b for a, b in zip(seed, keystream))

    total = tiles * PAYLOAD_PER_TILE                       # 576
    payload = bytearray(total)
    payload[0:len(magic) + 1] = magic + b"\x00"
    payload[8:40] = masked_seed
    payload[40:56] = tag
    for i in range(56, total):
        payload[i] = stone_byte(salt, 0x5A, i)

    order = tile_permutation(attestation, tiles)
    chunks = [bytes(payload[j * PAYLOAD_PER_TILE:(j + 1) * PAYLOAD_PER_TILE])
              for j in range(tiles)]

    # `order[slot]` is the file position that carries payload slot `slot`.
    out = bytearray(tiles * TILE_BYTES)
    for slot in range(tiles):
        file_index = order[slot]
        # keep the *front* of each tile as plain stone: the solver can use it
        # to prove the stone layer is intact before trusting the tail bytes.
        stone = bytearray(stone_byte(salt, file_index, i) & 0xFC for i in range(TILE_BYTES))
        chunk = chunks[slot]
        for byte_index, b in enumerate(chunk):
            for k in range(STONE_BYTES_PER_PAYLOAD_BYTE):
                # payload runs from the END of the tile backwards
                lo = TILE_BYTES - 1 - byte_index * STONE_BYTES_PER_PAYLOAD_BYTE - k
                stone[lo] = (stone[lo] & 0xFC) | ((b >> (k * BITS_PER_STONE_BYTE)) & 0x03)
        out[file_index * TILE_BYTES:(file_index + 1) * TILE_BYTES] = stone

    return bytes(out), order


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    config_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "config.json")
    output_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        root, "app", "src", "main", "assets", "mosaic_wells.bin")
    attestation = sys.argv[3] if len(sys.argv) > 3 else "HIDIMBI_ATTESTATION_UNSEALED"

    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)

    data, order = build_mosaic(config, attestation)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(data)

    uc = config["under_city"]
    print(f"[+] Wrote mosaic {output_path} ({len(data)} bytes, "
          f"{uc['mosaic_tiles']} tiles of {uc['mosaic_tile_bytes']} bytes)")
    print(f"[+] Tile order (payload slot -> file index): {order}")
    print("[!] Regenerate with the live Stage-12 attestation before deploying, "
          "otherwise the seed is not reachable.")


if __name__ == "__main__":
    main()
