#!/bin/sh
# ---------------------------------------------------------------------------
# 10_provision.sh — runs inside the vault container as the initdb hook.
#
# Sets the two runtime role passwords and applies the least-privilege grants.
# It does NOT need to insert a flag row: flag rows are created lazily per team
# the first time a player's ticket is seen (vault.ensure_team_flag), so no flag
# is ever materialised for a team that has not earned one.
#
# ORDER MATTERS: every REVOKE must run before every GRANT, otherwise the
# blanket "REVOKE ALL ON ALL FUNCTIONS" wipes the specific function grants.
# ---------------------------------------------------------------------------
set -e

DB="${POSTGRES_DB:-hidimbi_vault}"
# the shared runtime password, injected by the platform
RUNTIME_PASSWORD="${DB_PASSWORD:?DB_PASSWORD must be set for the runtime roles}"
ROTATOR_PASSWORD="${ROTATOR_PASSWORD:-${RUNTIME_PASSWORD}}"

psql_as_admin() {
    psql -v ON_ERROR_STOP=1 --username "${POSTGRES_USER}" --dbname "$DB" "$@"
}

echo "[*] vault: setting runtime role passwords"

psql_as_admin <<SQL
DO \$\$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'vault_flagger') THEN
    CREATE ROLE vault_flagger LOGIN PASSWORD '${RUNTIME_PASSWORD}';
  ELSE
    ALTER ROLE vault_flagger PASSWORD '${RUNTIME_PASSWORD}';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'vault_rotator') THEN
    CREATE ROLE vault_rotator LOGIN PASSWORD '${ROTATOR_PASSWORD}';
  ELSE
    ALTER ROLE vault_rotator PASSWORD '${ROTATOR_PASSWORD}';
  END IF;
END
\$\$;
SQL

echo "[*] vault: applying least-privilege grants"

# --- step 1: strip everything the runtime roles may have inherited -----------
psql_as_admin <<'SQL'
REVOKE ALL ON SCHEMA vault      FROM vault_flagger, vault_rotator;
REVOKE ALL ON ALL TABLES    IN SCHEMA vault FROM vault_flagger, vault_rotator;
REVOKE ALL ON ALL SEQUENCES IN SCHEMA vault FROM vault_flagger, vault_rotator;
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA vault FROM vault_flagger, vault_rotator;
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA vault FROM PUBLIC;
ALTER DEFAULT PRIVILEGES IN SCHEMA vault
    REVOKE ALL ON TABLES    FROM vault_flagger, vault_rotator;
ALTER DEFAULT PRIVILEGES IN SCHEMA vault
    REVOKE ALL ON SEQUENCES FROM vault_flagger, vault_rotator;
ALTER DEFAULT PRIVILEGES IN SCHEMA vault
    REVOKE ALL ON FUNCTIONS FROM vault_flagger, vault_rotator;
SQL

# --- step 2: then grant back only what each role may do ----------------------
psql_as_admin <<'SQL'
GRANT USAGE ON SCHEMA vault TO vault_flagger, vault_rotator;

-- flagger: exactly four functions. No table SELECT anywhere.
GRANT EXECUTE ON FUNCTION
    vault.claim_by_selector(text, text, bytea, inet)          TO vault_flagger;
GRANT EXECUTE ON FUNCTION
    vault.stub_for_team(text, text)                           TO vault_flagger;
GRANT EXECUTE ON FUNCTION
    vault.ensure_team_flag(text, text, bytea, integer)        TO vault_flagger;
GRANT EXECUTE ON FUNCTION
    vault.log_rejection(text, text, text, text, bytea, inet)  TO vault_flagger;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA vault          TO vault_flagger;
GRANT SELECT, INSERT ON vault.flag_rejections                 TO vault_flagger;

-- rotator: exactly one function.
GRANT EXECUTE ON FUNCTION
    vault.rotate(text, text, integer)                         TO vault_rotator;

-- Default-deny the flag tables. Every function above is SECURITY DEFINER and
-- owned by the superuser, so they keep working; all other access is blocked.
ALTER TABLE vault.flag_instances ENABLE ROW LEVEL SECURITY;
ALTER TABLE vault.flag_redemptions ENABLE ROW LEVEL SECURITY;
SQL

echo "[+] vault: provisioned. flag rows are created lazily per team."
