#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# scripts/dev_stack.sh
#
# Spins up the whole challenge on a developer machine WITHOUT Docker:
#   1. a real PostgreSQL server on its own port (the external datastore)
#   2. the Lake API + Observatory            -> :8000
#   3. the flag service (public)             -> :8001
#   4. the Keeper's internal index           -> :8100
#
# These are separate OS processes with separate listening sockets, mirroring
# the three-container compose topology. The flag value is generated inside
# PostgreSQL and never lands in a file.
#
#   ./scripts/dev_stack.sh up | solve | rotate | down
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND="$ROOT/backend"
STATE="${HIDIMBI_STATE:-/private/var/folders/13/qqzpdd8j4bb6s6n48m_wz75r0000gn/T/opencode/hidimbi_stack}"
mkdir -p "$STATE"

PGDATA="${PGDATA:-/private/var/folders/13/qqzpdd8j4bb6s6n48m_wz75r0000gn/T/opencode/hidimbi_vault_pg}"
PGPORT="${HIDIMBI_DB_PORT:-5433}"
DBNAME="hidimbi_vault"
INSTANCE="${HIDIMBI_INSTANCE_ID:-hidimbi-main}"
TEAM="${HIDIMBI_TEAM_ID:-solo}"
TTL="${HIDIMBI_FLAG_TTL_HOURS:-24}"
BACKEND_PORT=8000
FLAGGER_PORT=8001
INTERNAL_PORT=8100

# Owner connection (initdb superuser, trust auth — dev only)
OWNER_URI="postgresql://vault_owner@127.0.0.1:${PGPORT}"
ADMIN_URI="postgresql://vault_owner@127.0.0.1:${PGPORT}/${DBNAME}"

export PGCONNECT_TIMEOUT=5
export PGUSER=vault_owner
export PATH="/opt/homebrew/bin:$PATH"

# --- locate a Python that actually has the challenge's dependencies -------
find_py() {
  for cand in \
      "${PYTHON_BIN:-}" \
      "$(command -v python3)" \
      /opt/anaconda3/bin/python3 \
      /opt/homebrew/bin/python3.13 \
      /usr/local/bin/python3 \
      /usr/bin/python3; do
    [ -n "$cand" ] || continue
    command -v "$cand" >/dev/null 2>&1 || continue
    "$cand" -c "import fastapi, uvicorn, psycopg2" >/dev/null 2>&1 && { echo "$cand"; return 0; }
  done
  return 1
}
PY="$(find_py)" || { err "no python3 with fastapi/uvicorn/psycopg2 found"; exit 1; }
log "using interpreter: $PY"

log() { printf '\033[1;36m[*]\033[0m %s\n' "$*"; }
err() { printf '\033[1;31m[-]\033[0m %s\n' "$*" >&2; }


start_db() {
  if pg_isready -h 127.0.0.1 -p "$PGPORT" >/dev/null 2>&1; then
    log "postgres already running on :$PGPORT"
  else
    if [ ! -s "$PGDATA/PG_VERSION" ]; then
      log "initdb at $PGDATA"
      rm -rf "$PGDATA"; mkdir -p "$PGDATA"
      initdb -D "$PGDATA" -U vault_owner --auth=trust -E UTF8 >/dev/null
    fi
    pg_ctl -D "$PGDATA" -o "-p $PGPORT" -l "$PGDATA/server.log" start >/dev/null
    for _ in $(seq 1 40); do
      pg_isready -h 127.0.0.1 -p "$PGPORT" >/dev/null 2>&1 && break
      sleep 1
    done
  fi
  pg_isready -h 127.0.0.1 -p "$PGPORT" >/dev/null 2>&1 || { err "postgres did not start"; exit 1; }

  if ! psql "$OWNER_URI/postgres" -Atq -c "SELECT 1" >/dev/null 2>&1; then
    err "cannot connect as vault_owner"; exit 1
  fi
  if ! psql "$ADMIN_URI" -Atq -c "SELECT 1" >/dev/null 2>&1; then
    psql "$OWNER_URI/postgres" -q -c "CREATE DATABASE $DBNAME" >/dev/null
  fi

  psql "$ADMIN_URI" -v ON_ERROR_STOP=1 -q -f "$BACKEND/db/init/01_schema.sql" \
    | grep -v NOTICE || true

  if [ ! -s "$STATE/passwords.env" ]; then
    openssl rand -hex 32 > "$STATE/roles.password"
    openssl rand -hex 32 > "$STATE/rotator.password"
    openssl rand -hex 16 > "$STATE/instance.salt"
    openssl rand -hex 32 > "$STATE/flagger_proof_key.hex"
    openssl rand -hex 32 > "$STATE/backend_bearer.hex"
    printf 'DB_PASSWORD=%s\nROTATOR_PASSWORD=%s\nHIDIMBI_FLAG_SALT=%s\nFLAGGER_PROOF_KEY=%s\nBACKEND_BEARER_SECRET=%s\n' \
      "$(cat "$STATE/roles.password")" "$(cat "$STATE/rotator.password")" \
      "$(cat "$STATE/instance.salt")" "$(cat "$STATE/flagger_proof_key.hex")" \
      "$(cat "$STATE/backend_bearer.hex")" > "$STATE/passwords.env"
  fi
  set -a; . "$STATE/passwords.env"; set +a

  psql "$ADMIN_URI" -v ON_ERROR_STOP=1 -q <<SQL >/dev/null
CREATE EXTENSION IF NOT EXISTS pgcrypto;
DO \$\$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname='vault_flagger') THEN
    CREATE ROLE vault_flagger LOGIN PASSWORD '${DB_PASSWORD}';
  ELSE
    ALTER ROLE vault_flagger WITH LOGIN PASSWORD '${DB_PASSWORD}';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname='vault_rotator') THEN
    CREATE ROLE vault_rotator LOGIN PASSWORD '${ROTATOR_PASSWORD}';
  ELSE
    ALTER ROLE vault_rotator WITH LOGIN PASSWORD '${ROTATOR_PASSWORD}';
  END IF;
END
\$\$;
REVOKE ALL ON SCHEMA vault FROM vault_flagger, vault_rotator;
GRANT USAGE ON SCHEMA vault TO vault_flagger, vault_rotator;
REVOKE ALL ON ALL TABLES IN SCHEMA vault FROM vault_flagger, vault_rotator;
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA vault FROM vault_flagger, vault_rotator;
GRANT EXECUTE ON FUNCTION vault.claim_by_selector(text,text,bytea,inet) TO vault_flagger;
GRANT EXECUTE ON FUNCTION vault.stub_for_team(text,text) TO vault_flagger;
GRANT EXECUTE ON FUNCTION vault.log_rejection(text,text,text,text,bytea,inet) TO vault_flagger;
GRANT EXECUTE ON FUNCTION vault.rotate(text,text,integer) TO vault_rotator;
GRANT SELECT, INSERT ON vault.flag_rejections TO vault_flagger;
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA vault TO vault_flagger;
SQL


  # Provision if empty. The team id is derived exactly the way a solver
  # derives it (SHA256(ANDROID_ID || K4)[:12] behind "team_"), so the flagger's
  # stage-12 gate finds a live row for the pilgrim that shows up.
  CURRENT_TEAM="$(CONFIG_JSON_PATH="$ROOT/config.json" python3 - <<'PYEOF'
import hashlib, json, os
cfg = json.load(open(os.environ["CONFIG_JSON_PATH"]))
k4 = cfg["keys"]["k4"]
android_id = "d4e5f6a1b2c30077"          # the debug device in the solve script
print(hashlib.sha256((android_id + k4).encode()).hexdigest()[:12])
PYEOF
)"
  TEAM_NAME="$CURRENT_TEAM"
  if [ "$(psql "$ADMIN_URI" -Atq -c 'SELECT count(*) FROM vault.flag_instances')" = "0" ]; then
    # The selector is a keyed SHA-256 over the full chain material. It is
    # derived HERE exactly the way solve_scripts/solve_hidimbi.py and
    # backend/flag_service.py derive it; all three must agree.
    SELECTOR="$(CONFIG_PATH="$ROOT/config.json" HIDIMBI_TEAM_NAME="$TEAM_NAME" "$PY" - <<'PYEOF'
import hashlib, json, os
cfg = json.load(open(os.environ["CONFIG_PATH"]))
K = [bytes.fromhex(cfg["keys"][k]) for k in ("k1", "k2", "k3", "k4")]
N = [bytes.fromhex(cfg["nonces"][n]) for n in ("n1", "n2", "n3", "n4")]
u = cfg["under_city"]
team = os.environ["HIDIMBI_TEAM_NAME"]
material = (
    b"HIDIMBI_TOWN_SELECTOR_v1"
    + b"".join(K) + b"".join(N)
    + bytes.fromhex(u["scriptorium_seed"]) + bytes.fromhex(u["drum_key"])
    + bytes.fromhex(u["scribe_rune"]) + bytes.fromhex(u["granary_secret"])
    + bytes.fromhex(u["whistle_key"])
    + f"hidimbi-main:{team}".encode()
)
print(hashlib.sha256(material).hexdigest())
PYEOF
)"
    psql "$ADMIN_URI" -v ON_ERROR_STOP=1 -q -c \
      "SELECT vault.provision_with_selector('${INSTANCE}','${TEAM_NAME}',1,decode('${SELECTOR}','hex'),${TTL});" >/dev/null
  fi
  log "vault provisioned (instance=${INSTANCE} team=${TEAM_NAME})"
}

start_services() {
  set -a; . "$STATE/passwords.env"; set +a
  TEAM_NAME="${TEAM_NAME:-${TEAM:-solo}}"
  export TEAM_NAME="${TEAM_NAME:-$CURRENT_TEAM}"


  env_common=(
    CONFIG_PATH="$ROOT/config.json"
    DB_HOST=127.0.0.1 DB_PORT="$PGPORT" DB_NAME="$DBNAME"
    DB_USER=vault_flagger DB_PASSWORD="$DB_PASSWORD"
    FLAGGER_PROOF_KEY="$FLAGGER_PROOF_KEY"
    INSTANCE_ID="$INSTANCE"
  )

  if ! curl -sf "http://127.0.0.1:${BACKEND_PORT}/healthz" >/dev/null 2>&1; then
    ( cd "$BACKEND" && FLAGGER_HOST=127.0.0.1 FLAGGER_PORT="$FLAGGER_PORT" \
      FLAGGER_INTERNAL_PORT="$INTERNAL_PORT" COURIER_INTERNAL_TARGET="127.0.0.1:$INTERNAL_PORT" env "${env_common[@]}" \
      nohup "$PY" -m uvicorn server:app --host 127.0.0.1 --port "$BACKEND_PORT" \
        --no-access-log > "$STATE/lake.log" 2>&1 & )
    log "lake API on :$BACKEND_PORT"
  fi

  if ! curl -sf "http://127.0.0.1:${FLAGGER_PORT}/healthz" >/dev/null 2>&1; then
    ( cd "$BACKEND" && env "${env_common[@]}" \
      nohup "$PY" -m uvicorn flag_service:app --host 127.0.0.1 --port "$FLAGGER_PORT" \
        --no-access-log > "$STATE/flagger.log" 2>&1 & )
    log "flag service on :$FLAGGER_PORT"
  fi

  if ! curl -sf "http://127.0.0.1:${INTERNAL_PORT}/healthz" >/dev/null 2>&1; then
    ( cd "$BACKEND" && env "${env_common[@]}" \
      nohup "$PY" -m uvicorn flag_service:app --host 127.0.0.1 --port "$INTERNAL_PORT" \
        --no-access-log > "$STATE/keeper.log" 2>&1 & )
    log "keeper internal index on :$INTERNAL_PORT"
  fi

  for p in "$BACKEND_PORT" "$FLAGGER_PORT" "$INTERNAL_PORT"; do
    for _ in $(seq 1 40); do
      curl -sf "http://127.0.0.1:$p/healthz" >/dev/null 2>&1 && break
      sleep 0.5
    done
    curl -sf "http://127.0.0.1:$p/healthz" >/dev/null 2>&1 || {
      err "service on :$p did not become healthy"; tail -20 "$STATE"/*.log; exit 1; }
  done
  log "all services healthy"
}

case "${1:-up}" in
  up)
    start_db
    start_services
    cat > "$STATE/stack.env" <<EOF
export HIDIMBI_BACKEND=http://127.0.0.1:${BACKEND_PORT}
export HIDIMBI_FLAGGER=http://127.0.0.1:${FLAGGER_PORT}
export HIDIMBI_KEEPER=http://127.0.0.1:${INTERNAL_PORT}
export HIDIMBI_KEEPER_ALIAS=flagger
export HIDIMBI_DB_PORT=${PGPORT}
export HIDIMBI_INSTANCE_ID=${INSTANCE}
export HIDIMBI_TEAM_ID=${TEAM_NAME:-${TEAM}}
EOF
    log "wrote $STATE/stack.env"
    echo
    echo "  Lake API      http://127.0.0.1:${BACKEND_PORT}"
    echo "  Flag service  http://127.0.0.1:${FLAGGER_PORT}"
    echo "  Keeper index  http://127.0.0.1:${INTERNAL_PORT}   (SSRF target)"
    echo "  PostgreSQL    127.0.0.1:${PGPORT}/${DBNAME}        (external datastore)"
    ;;
  solve)
    # shellcheck disable=SC1090
    . "$STATE/stack.env"
    exec "$PY" "$ROOT/solve_scripts/solve_hidimbi.py" \
      --backend "$HIDIMBI_BACKEND" \
      --flag-service "$HIDIMBI_FLAGGER" \
      --keeper "$HIDIMBI_KEEPER" \
      --instance "$HIDIMBI_INSTANCE_ID" --team "$HIDIMBI_TEAM_ID"
    ;;
  rotate)
    . "$STATE/passwords.env"
    TEAM_NAME="${HIDIMBI_TEAM_NAME:-solo}"
    psql "$ADMIN_URI" -c \
      "SELECT vault.rotate('${INSTANCE}','${TEAM_NAME}',${TTL});"
    echo "[+] rotated; new live row:"
    psql "$ADMIN_URI" -Atc \
      "SELECT epoch_id, flag_value FROM vault.flag_instances \
        WHERE consumed=false AND (expires_at IS NULL OR expires_at > now()) \
        ORDER BY epoch_id DESC LIMIT 1"
    ;;
  down)
    pkill -f "uvicorn server:app" 2>/dev/null || true
    pkill -f "uvicorn flag_service:app" 2>/dev/null || true
    pg_ctl -D "$PGDATA" stop -m fast >/dev/null 2>&1 || true
    log "stack stopped"
    ;;
  *) echo "usage: $0 up|solve|rotate|down" >&2; exit 2 ;;
esac
