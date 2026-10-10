#!/usr/bin/env python3
"""
tools/render_init_vault.py

One-shot initialisation of a **Render Managed PostgreSQL** database.

Render's managed Postgres runs the postgres image without the /docker-entrypoint
-initdb.d hook, so backend/db/init/*.sql never gets applied automatically.
This script does the same job, in the same order, over a normal connection.

Run it ONCE, right after you create the Render Postgres instance, from your
laptop, using the *External* Database URL (the Internal one is only reachable
from inside Render):

    export DATABASE_URL="postgres://user:pass@host:5432/dbname"
    export DB_PASSWORD="$(openssl rand -hex 32)"
    export ROTATOR_PASSWORD="$(openssl rand -hex 32)"
    python3 tools/render_init_vault.py

Order matters, and it is not the same as the file order in db/init/:
  1. roles            (00_create_roles.sql)  — must exist before anything else
  2. schema+functions (01_schema.sql)
  3. passwords + grants                       — LAST, and all REVOKEs before
                                                 all GRANTs

It is idempotent: safe to re-run. Re-running will not consume a flag row.
"""

import os
import sys

import psycopg2
import psycopg2.extras

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ROLES_SQL = os.path.join(ROOT, "backend", "db", "init", "00_create_roles.sql")
SCHEMA_SQL = os.path.join(ROOT, "backend", "db", "init", "01_schema.sql")

GRANTS_SQL = """
GRANT USAGE ON SCHEMA vault TO vault_flagger, vault_rotator;

-- flagger: exactly four functions, and no SELECT on any table.
GRANT EXECUTE ON FUNCTION vault.claim_by_selector(text, text, bytea, inet)
    TO vault_flagger;
GRANT EXECUTE ON FUNCTION vault.stub_for_team(text, text)
    TO vault_flagger;
GRANT EXECUTE ON FUNCTION vault.ensure_team_flag(text, text, bytea, integer)
    TO vault_flagger;
GRANT EXECUTE ON FUNCTION vault.log_rejection(text, text, text, text, bytea, inet)
    TO vault_flagger;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA vault TO vault_flagger;
GRANT SELECT, INSERT ON vault.flag_rejections TO vault_flagger;

-- rotator: exactly one function.
GRANT EXECUTE ON FUNCTION vault.rotate(text, text, integer) TO vault_rotator;

-- Default-deny the flag tables. Every function above is SECURITY DEFINER and
-- owned by the database owner, so they keep working.
ALTER TABLE vault.flag_instances ENABLE ROW LEVEL SECURITY;
ALTER TABLE vault.flag_instances FORCE ROW LEVEL SECURITY;
ALTER TABLE vault.flag_redemptions ENABLE ROW LEVEL SECURITY;
ALTER TABLE vault.flag_redemptions FORCE ROW LEVEL SECURITY;
"""


def connect():
    url = os.environ.get("DATABASE_URL") or os.environ.get("RENDER_DATABASE_URL")
    if not url:
        sys.exit("[!] set DATABASE_URL to the Render PostgreSQL External URL")
    return psycopg2.connect(url)


def strip_everything(cur):
    """All REVOKEs first — otherwise REVOKE ALL ON ALL FUNCTIONS wipes the
    specific grants applied afterwards."""
    cur.execute("REVOKE ALL ON SCHEMA vault FROM vault_flagger, vault_rotator;")
    cur.execute("REVOKE ALL ON ALL TABLES    IN SCHEMA vault "
                "FROM vault_flagger, vault_rotator;")
    cur.execute("REVOKE ALL ON ALL SEQUENCES IN SCHEMA vault "
                "FROM vault_flagger, vault_rotator;")
    cur.execute("REVOKE ALL ON ALL FUNCTIONS IN SCHEMA vault "
                "FROM vault_flagger, vault_rotator;")


def main():
    runtime_pw = os.environ.get("DB_PASSWORD")
    rotator_pw = os.environ.get("ROTATOR_PASSWORD") or runtime_pw
    if not runtime_pw:
        sys.exit("[!] set DB_PASSWORD (the vault_flagger password)")

    conn = connect()
    conn.autocommit = False
    try:
        with conn.cursor() as cur:
            print("[*] 1/4 creating roles")
            cur.execute(open(ROLES_SQL).read())

            print("[*] 2/4 applying schema and functions")
            cur.execute(open(SCHEMA_SQL).read())

            print("[*] 3/4 setting runtime role passwords")
            cur.execute(
                "DO $$ BEGIN "
                "  IF NOT EXISTS (SELECT 1 FROM pg_roles "
                "                WHERE rolname='vault_flagger') THEN "
                "    CREATE ROLE vault_flagger LOGIN PASSWORD %s; "
                "  ELSE "
                "    ALTER ROLE vault_flagger PASSWORD %s; "
                "  END IF; "
                "  IF NOT EXISTS (SELECT 1 FROM pg_roles "
                "                WHERE rolname='vault_rotator') THEN "
                "    CREATE ROLE vault_rotator LOGIN PASSWORD %s; "
                "  ELSE "
                "    ALTER ROLE vault_rotator PASSWORD %s; "
                "  END IF; END $$;",
                (runtime_pw, runtime_pw, rotator_pw, rotator_pw))

            print("[*] 4/4 applying least-privilege grants")
            strip_everything(cur)
            cur.execute(GRANTS_SQL)

        conn.commit()
    except Exception as exc:
        conn.rollback()
        sys.exit(f"[!] initialisation failed, nothing was committed: {exc}")
    finally:
        conn.close()

    print()
    print("[+] Render Vault initialised.")
    print("    vault_flagger : EXECUTE on 4 functions, no table SELECT")
    print("    vault_rotator : EXECUTE on vault.rotate only")
    print("    flag_instances: RLS enabled and forced (default-deny)")
    print()
    print("[*] flag rows are created lazily per team at the Stage-12 gate,")
    print("    so there is nothing to insert here.")


if __name__ == "__main__":
    main()
