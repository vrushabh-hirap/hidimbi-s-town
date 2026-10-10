#!/usr/bin/env python3
"""
tools/gen_cave_store.py
Compiles config.json into a custom native binary container cave_store.bin.
Replaces the old SQLite database completely, allowing instant configuration
and clean deployment.
"""

import sys
import os
import json
import struct

def fletcher16(data: bytes) -> int:
    sum1 = 0
    sum2 = 0
    for byte in data:
        sum1 = (sum1 + byte) % 255
        sum2 = (sum2 + sum1) % 255
    return (sum2 << 8) | sum1

def build_record(tag: int, key: str, value: bytes) -> bytes:
    key_bytes = key.encode("utf-8")
    key_len = len(key_bytes)
    val_len = len(value)
    
    # Tag (1 byte) + key_len (1 byte) + val_len (2 bytes big endian)
    header = struct.pack(">BBH", tag, key_len, val_len)
    body = header + key_bytes + value
    chk = fletcher16(body)
    record = body + struct.pack(">H", chk)
    return record

def main():
    if len(sys.argv) < 3:
        config_path = os.path.join(os.path.dirname(__file__), "..", "config.json")
        output_path = os.path.join(os.path.dirname(__file__), "..", "app", "src", "main", "assets", "cave_store.bin")
    else:
        config_path = sys.argv[1]
        output_path = sys.argv[2]

    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    magic = b"\xDE\xAD\xCA\xFE"
    version = 1
    
    k1_hex = config["keys"]["k1"]
    n1_hex = config["nonces"]["n1"]
    decoy_4 = config["decoys"]["decoy_4"]

    k1_bytes = bytes.fromhex(k1_hex)
    n1_bytes = bytes.fromhex(n1_hex)
    real_secret = k1_bytes + n1_bytes

    records = []
    # Record 1: Metadata
    records.append(build_record(0x01, "realm", b"Cave of Hidimbi - Sanctum"))
    # Record 2: Decoy Secret (returned for naive "secret" query)
    records.append(build_record(0x02, "secret", decoy_4.encode("utf-8")))
    # Record 3: Protected True Keys (leaked via native parser signed offset flaw)
    records.append(build_record(0x03, "sanctum_vault", real_secret))

    header = magic + struct.pack(">HH", version, len(records))
    payload = header + b"".join(records)

    with open(output_path, "wb") as f:
        f.write(payload)

    print(f"[+] Successfully compiled {output_path} ({len(payload)} bytes) from {config_path}")

if __name__ == "__main__":
    main()
