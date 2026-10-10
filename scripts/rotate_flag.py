#!/usr/bin/env python3
"""Rotate the flag epochs held by the vault datastore."""

import argparse
import asyncio
import os
import sys

import asyncpg


async def main() -> None:
    parser = argparse.ArgumentParser(description="Rotate flag epochs")
    parser.add_argument("--host", default=os.environ.get("HIDIMBI_DB_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int,
                        default=int(os.environ.get("HIDIMBI_DB_PORT", "5433")))
    parser.add_argument("--db", default=os.environ.get("HIDIMBI_DB_NAME", "hidimbi_vault"))
    parser.add_argument("--user", default=os.environ.get("HIDIMBI_DB_USER", "vault_rotator"))
    parser.add_argument("--password",
                        default=os.environ.get("HIDIMBI_DB_PASSWORD"),
                        required=not os.environ.get("HIDIMBI_DB_PASSWORD"))
    parser.add_argument("--instance", default=os.environ.get("HIDIMBI_INSTANCE_ID", "hidimbi-main"))
    parser.add_argument("--team", default=os.environ.get("HIDIMBI_TEAM_ID", "solo"))
    parser.add_argument("--ttl", type=int, default=24)
    args = parser.parse_args()

    conn = await asyncpg.connect(host=args.host, port=args.port, database=args.db,
                                 user=args.user, password=args.password)
    try:
        new_epoch = await conn.fetchval("SELECT vault.rotate($1, $2, $3)",
                                        args.instance, args.team, args.ttl)
        print(f"[+] rotated to epoch {new_epoch} for {args.instance}/{args.team}")
    finally:
        await conn.close()


if __name__ == "__main__":
    asyncio.run(main())
