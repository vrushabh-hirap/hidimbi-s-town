#!/usr/bin/env python3
"""
backend/provision_vault.py
Hidimbi's Town — Vault provisioning and rotation.

This is the ONLY program that ever writes a flag value, and it never reads one
from `config.json`. Suffixes come from the platform environment
(`HIDIMBI_FLAG_SUFFIXES`) or, when that is absent, from the OS CSPRNG. The
one-time manifest it prints is the only place the plaintext flags ever land,
and it goes to stdout — into the CTF platform's provisioning hook, not into a
file inside the repository.

Usage
-----
    # 1. create the schema and the three least-privilege roles
    python3 provision_vault.py --init

    # 2. provision flag rows for the teams this instance serves
    HIDIMBI_TEAMS=team_a,team_b python3 provision_vault.py --provision

    # 3. rotate to a new epoch with fresh per-team flags
    HIDIMBI_NEXT_EPOCH=2 python3 provision_vault.py --rotate

    # 4. inspect (never prints flag_value)
    python3 provision_vault.py --status
"""

import argparse
import json
import os
import secrets
import sys

import psycopg2

from chain_selector import vault_selector
import db_access

SCHEMA_DIR = os.path.join(db_access.BASE_DIR, "db", "init")

FLAG_SUFFIX_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"   # no I, O, 0, 1
FLAG_SUFFIX_LENGTH = 12


def new_flag_suffix() -> str:
    """CSPRNG suffix, unique per instance and per team."""
    return "".join(secrets.choice(FLAG_SUFFIX_ALPHABET) for _ in range(FLAG_SUFFIX_LENGTH))


def owner_connect():
    dsn = (
        f"host={os.environ.get('HIDIMBI_OWNER_DB_HOST', '127.0.0.1')} "
        f"port={os.environ.get('HIDIMBI_OWNER_DB_PORT', os.environ.get('HIDIMBI_DB_PORT', '5433'))} "
        f"dbname={os.environ.get('HIDIMBI_OWNER_DB_NAME', 'hidimbi_vault')} "
        f"user={os.environ.get('HIDIMBI_OWNER_DB_USER')} "
        f"password={os.environ.get('HIDIMBI_OWNER_DB_PASSWORD')} connect_timeout=5"
    )
    return psycopg2.connect(dsn)


def run_init():
    with owner_connect() as conn, conn.cursor() as cur:
        for name in ("01_schema.sql", "02_roles.sql"):
            path = os.path.join(SCHEMA_DIR, name)
            with open(path, "r", encoding="utf-8") as f:
                cur.execute(f.read())
            print(f"[+] applied {name}")
    passwords = ["HIDIMBI_OWNER_DB_PASSWORD", "HIDIMBI_BACKEND_DB_PASSWORD",
                 "HIDIMBI_FLAGGER_DB_PASSWORD"]
    if any(not os.environ.get(p) for p in passwords):
        print("[!] role passwords are not set — apply them with ALTER ROLE before deploying")


def team_list() -> list:
    raw = os.environ.get("HIDIMBI_TEAMS", "")
    return [t.strip() for t in raw.split(",") if t.strip()]


def provision(teams=None):
    teams = teams or team_list()
    if not teams:
        print("[!] no teams supplied (HIDIMBI_TEAMS); nothing to provision")
        return 0

    ttl = int(os.environ.get("HIDIMBI_FLAG_TTL_SECONDS",
                             db_access.VAULT_CFG.get("flag_ttl_seconds", 21600)))
    manifest = {}
    with owner_connect() as conn, conn.cursor() as cur:
        cur.execute(
            "INSERT INTO vault.challenge_epoch (epoch_id, epoch_label) "
            "VALUES (%s, %s) ON CONFLICT (epoch_id) DO NOTHING",
            (db_access.EPOCH_ID, f"epoch-{db_access.EPOCH_ID}"))
        if not os.environ.get("HIDIMBI_FLAG_SUFFIXES"):
            print("[!] Generating CSPRNG suffixes — record the manifest, it is not retrievable.")
        suffix_map = json.loads(os.environ.get("HIDIMBI_FLAG_SUFFIXES", "{}"))

        for team in teams:
            suffix = suffix_map.get(team) or new_flag_suffix()
            suffix_map[team] = suffix
            selector = vault_selector(db_access.CONFIG, db_access.INSTANCE_ID, team,
                                      db_access.EPOCH_ID)
            cur.execute(
                "INSERT INTO vault.team_flag "
                " (instance_id, team_id, epoch_id, selector_sha, flag_value, expires_at) "
                "VALUES (%s, %s, %s, %s, %s, now() || ' seconds'::interval * %s) "
                "ON CONFLICT (instance_id, team_id) DO NOTHING",
                (db_access.INSTANCE_ID, team, db_access.EPOCH_ID,
                 psycopg2.Binary(selector),
                 "KCTF{HIDIMBI_TOWN_" + suffix + "}", ttl))
            manifest[team] = "KCTF{HIDIMBI_TOWN_" + suffix + "}"

    print("[+] provisioned %d flag rows for instance %s (epoch %d, ttl %ds)"
          % (len(teams), db_access.INSTANCE_ID, db_access.EPOCH_ID, ttl))
    if os.environ.get("HIDIMBI_PRINT_MANIFEST", "1") == "1":
        print(json.dumps(manifest, indent=2))
    return len(teams)


def rotate():
    nxt = int(os.environ.get("HIDIMBI_NEXT_EPOCH", db_access.EPOCH_ID + 1))
    ttl = int(os.environ.get("HIDIMBI_FLAG_TTL_SECONDS",
                             db_access.VAULT_CFG.get("flag_ttl_seconds", 21600)))
    suffix_map = {}
    for team in team_list():
        suffix_map[team] = new_flag_suffix()
    with owner_connect() as conn, conn.cursor() as cur:
        cur.execute(
            "SELECT vault.rotate_flags(%s, %s, %s, %s, %s::jsonb)",
            (db_access.INSTANCE_ID, nxt, f"epoch-{nxt}", ttl,
             json.dumps(suffix_map)))
        rotated = cur.fetchone()[0]

    # rotate the selector too: the epoch id is part of the selector material,
    # so each rotation re-keys every team's row.
    for team in suffix_map:
        selector = vault_selector(db_access.CONFIG, db_access.INSTANCE_ID, team, nxt)
        with owner_connect() as conn, conn.cursor() as cur:
            cur.execute(
                "UPDATE vault.team_flag SET selector_sha = %s "
                "WHERE instance_id = %s AND team_id = %s AND epoch_id = %s",
                (psycopg2.Binary(selector), db_access.INSTANCE_ID, team, nxt))

    manifest = {t: "KCTF{HIDIMBI_TOWN_" + s + "}" for t, s in suffix_map.items()}
    print("[+] rotated %d teams to epoch %d (ttl %ds)" % (rotated, nxt, ttl))
    print(json.dumps(manifest, indent=2))
    return rotated


def status():
    with owner_connect() as conn, conn.cursor() as cur:
        cur.execute("SELECT epoch_id, epoch_label, activated_at, retired_at "
                    "FROM vault.challenge_epoch ORDER BY epoch_id")
        print("\nepochs:")
        for row in cur.fetchall():
            print("  ", row)
        cur.execute(
            "SELECT instance_id, team_id, epoch_id, octet_length(selector_sha), "
            "expires_at, consumed, rotations FROM vault.team_flag "
            "ORDER BY instance_id, team_id")
        print("\nteam flag rows (flag_value never displayed):")
        for row in cur.fetchall():
            print("  ", row)
        cur.execute("SELECT count(*) FROM vault.redemption")
        print("\nredemptions:", cur.fetchone()[0])


def main():
    ap = argparse.ArgumentParser(description="Hidimbi's Town Vault provisioner")
    ap.add_argument("--init", action="store_true", help="apply schema and roles")
    ap.add_argument("--provision", action="store_true", help="create flag rows")
    ap.add_argument("--rotate", action="store_true", help="rotate to a new epoch")
    ap.add_argument("--status", action="store_true", help="inspect without revealing flags")
    args = ap.parse_args()

    try:
        if args.init:
            run_init()
        if args.provision:
            provision()
        if args.rotate:
            rotate()
        if args.status or not any(vars(args).values()):
            status()
    except psycopg2.Error as exc:
        print(f"[-] datastore error: {exc}", file=sys.stderr)
        raise SystemExit(2)


if __name__ == "__main__":
    main()
