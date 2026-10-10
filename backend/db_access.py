#!/usr/bin/env python3
"""
backend/db_access.py
Hidimbi's Town — external datastore access layer.

Every container that touches PostgreSQL goes through this module so that the
failure posture is uniform:

    * credentials come from the environment only — never from config.json,
      never from a file in the repository, never baked into an image layer.
    * every connection sets a statement_timeout so a wedged database cannot
      wedge the challenge with it.
    * every failure is "fail closed": a DBUnavailable / RedemptionFailure
      propagates to the caller, which returns a 503/4xx. There is no fallback
      flag, no cached flag and no "offline mode".
"""

import os
import json
import time
import psycopg2
import psycopg2.extras
import psycopg2.sql

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.environ.get("CONFIG_PATH", os.path.join(BASE_DIR, "..", "config.json"))
if not os.path.exists(CONFIG_PATH):
    CONFIG_PATH = os.path.join(BASE_DIR, "config.json")

with open(CONFIG_PATH, "r", encoding="utf-8") as f:
    CONFIG = json.load(f)

UNDER_CITY = CONFIG.get("under_city", {})
VAULT_CFG = CONFIG.get("vault", {})

INSTANCE_ID = os.environ.get("HIDIMBI_INSTANCE_ID", VAULT_CFG.get("instance_id", "HIDIMBI_LOCAL_DEV"))
EPOCH_ID = int(os.environ.get("HIDIMBI_EPOCH_ID", VAULT_CFG.get("epoch_id", 1)))


class DBUnavailable(Exception):
    """The datastore could not be reached. Callers MUST fail closed."""


class RedemptionFailure(Exception):
    """A redeem attempt was rejected by the database procedure."""

    # errcode -> (http status, participant-facing message)
    MAP = {
        "TICKET_NOT_FOUND":          (404, "No such attestation ticket."),
        "TICKET_CONSUMED":           (409, "This attestation ticket was already used."),
        "TICKET_EXPIRED":            (410, "The attestation ticket has expired; redo the courier's errand."),
        "SELECTOR_MISMATCH":         (403, "The vault selector does not match the chain."),
        "FLAG_ROW_MISSING":          (404, "No live flag row for this team."),
        "FLAG_EXPIRED_OR_CONSUMED":  (410, "The flag row has already been claimed or has rotated out."),
    }

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code
        self.status, self.message = self.MAP.get(code, (403, "The vault refused the claim."))


def _dsn(prefix: str) -> str:
    host = os.environ.get(f"{prefix}DB_HOST", "127.0.0.1")
    port = os.environ.get(f"{prefix}DB_PORT", os.environ.get("HIDIMBI_DB_PORT", "5433"))
    name = os.environ.get(f"{prefix}DB_NAME", "hidimbi_vault")
    user = os.environ.get(f"{prefix}DB_USER")
    password = os.environ.get(f"{prefix}DB_PASSWORD")
    if not user or not password:
        raise DBUnavailable(f"{prefix} credentials are not configured")
    return f"host={host} port={port} dbname={name} user={user} password={password} connect_timeout=5"


def connect(prefix: str = "HIDIMBI_", role: str = "flagger"):
    """Open a connection as one of the three least-privilege roles."""
    try:
        conn = psycopg2.connect(_dsn(f"HIDIMBI_{role.upper()}_"))
        conn.autocommit = True
        with conn.cursor() as cur:
            cur.execute("SET statement_timeout = '10s'")
        return conn
    except psycopg2.Error as exc:
        raise DBUnavailable(f"datastore unreachable for role {role}: {exc.__class__.__name__}") from exc


def redeem_flag(ticket_id: str, selector_sha: bytes, client_ip=None):
    """Run vault.redeem_flag — the single atomic statement that is the final
    stage of the challenge. Any failure is raised, never swallowed."""
    try:
        conn = connect(role="flagger")
    except DBUnavailable:
        raise
    try:
        with conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as cur:
            cur.execute(
                "SELECT flag_value, team_id, epoch_id "
                "FROM vault.redeem_flag(%s::uuid, %s::bytea, %s::inet)",
                (ticket_id, psycopg2.Binary(selector_sha), client_ip),
            )
            row = cur.fetchone()
            if not row:
                raise RedemptionFailure("FLAG_ROW_MISSING")
            return dict(row)
    except psycopg2.errors.RaiseException as exc:
        # plpgsql RAISE ... USING ERRCODE = 'P0001' arrives here
        code = (exc.diag.message_primary or "").strip()
        raise RedemptionFailure(code) from exc
    except psycopg2.Error as exc:
        raise DBUnavailable(f"redeem failed: {exc.__class__.__name__}") from exc
    finally:
        try:
            conn.close()
        except Exception:
            pass


def mint_ticket(team_id: str, selector_sha: bytes, ttl_seconds: int = 900, conn=None) -> str:
    """Called by the flagger's internal seal endpoint."""
    own = conn is None
    conn = conn or connect(role="flagger")
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT vault.mint_ticket(%s, %s, %s::bytea, %s)",
                (INSTANCE_ID, team_id, psycopg2.Binary(selector_sha), ttl_seconds),
            )
            return str(cur.fetchone()[0])
    except psycopg2.errors.RaiseException as exc:
        code = (exc.diag.message_primary or "").strip()
        if code == "TEAM_FLAG_NOT_LIVE":
            raise RedemptionFailure("FLAG_ROW_MISSING") from exc
        raise RedemptionFailure(code or "SELECTOR_MISMATCH") from exc
    except psycopg2.Error as exc:
        raise DBUnavailable(f"mint_ticket failed: {exc.__class__.__name__}") from exc
    finally:
        if own:
            try:
                conn.close()
            except Exception:
                pass
