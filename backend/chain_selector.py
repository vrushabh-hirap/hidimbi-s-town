#!/usr/bin/env python3
"""
backend/chain_selector.py
Hidimbi's Town — the vault selector.

This is the single place where the whole exploit chain collapses into the
32-byte key that addresses a flag row in the external datastore.

    selector = SHA-256(
        "HIDIMBI_VAULT_SELECTOR_v1" ||
        K1||K2||K3||K4        (Stages 5, 6, 7, 8)   ||
        N1||N2||N3||N4        (Stages 5, 6, 7, 8)   ||
        scriptorium_seed      (Stage 13, steganography)          ||
        drum_key              (Stage 15, format string)          ||
        scribe_rune           (Stage 16, protobuf wire abuse)    ||
        granary_secret        (Stage 17, TOCTOU race)            ||
        whistle_key           (Stage 20, timing side channel)    ||
        "<instance_id>:<team_id>:<epoch_id>"
    )

Nothing in that list is stored in the app, in an asset, or in any image: every
component has to actually be recovered by playing the challenge. The result is
what the final stage submits to the flag service, and what the flag service
hands to PostgreSQL as the query parameter.
"""

import hashlib

SELECTOR_LABEL = b"HIDIMBI_VAULT_SELECTOR_v1"


def chain_master(config):
    return (bytes.fromhex(config["keys"]["k1"]) +
            bytes.fromhex(config["keys"]["k2"]) +
            bytes.fromhex(config["keys"]["k3"]) +
            bytes.fromhex(config["keys"]["k4"]))


def chain_nonces(config):
    return (bytes.fromhex(config["nonces"]["n1"]) +
            bytes.fromhex(config["nonces"]["n2"]) +
            bytes.fromhex(config["nonces"]["n3"]) +
            bytes.fromhex(config["nonces"]["n4"]))


def vault_selector(config, instance_id: str, team_id: str, epoch_id: int) -> bytes:
    uc = config["under_city"]
    material = b"".join([
        SELECTOR_LABEL,
        chain_master(config),
        chain_nonces(config),
        bytes.fromhex(uc["scriptorium_seed"]),
        bytes.fromhex(uc["drum_key"]),
        bytes.fromhex(uc["scribe_rune"]),
        bytes.fromhex(uc["granary_secret"]),
        bytes.fromhex(uc["whistle_key"]),
        f"{instance_id}:{team_id}:{epoch_id}".encode("utf-8"),
    ])
    return hashlib.sha256(material).digest()
