#!/bin/sh
# ---------------------------------------------------------------------------
# entrypoint.sh — dispatches on HIDIMBI_ROLE so a single image serves all
# three runtime containers.
#
#   app     : Lake API + Observatory + Courier        -> :8000
#   flagger : flag service (public) + Keeper's index  -> :8001 + :8100
#   rotator : vault.rotate(), then stays alive
# ---------------------------------------------------------------------------
set -e

ROLE="${HIDIMBI_ROLE:-app}"
DB_PORT="${DB_PORT:-5432}"
DB_HOST="${DB_HOST:-vault}"
DB_NAME="${DB_NAME:-hidimbi_vault}"
DB_USER="${DB_USER:-vault_flagger}"

echo "[*] Hidimbi's Town container starting, role=${ROLE}"

case "$ROLE" in
  app)
    echo "[*] Lake API on :8000"
    exec uvicorn server:app --host 0.0.0.0 --port 8000 --log-level info
    ;;

  flagger)
    if [ -z "$FLAGGER_PROOF_KEY" ]; then
      echo "[!] FLAGGER_PROOF_KEY is required in production" >&2
      echo "[!] refusing to run with a default proof key" >&2
      exit 1
    fi
    # The public flag service (:8001) and the internal index (:8100) are the
    # same application; only one of the two ports is published.
    uvicorn flag_service:app --host 0.0.0.0 --port 8001 --log-level info &
    echo "[*] flag service (public) on :8001"
    echo "[*] Keeper's internal index on :8100"
    exec uvicorn flag_service:app --host 0.0.0.0 --port 8100 --log-level info
    ;;

  rotator)
    echo "[*] rotator waiting for vault at ${DB_HOST}:${DB_PORT}"
    until pg_isready -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" >/dev/null 2>&1; do
      sleep 3
    done
    echo "[*] rotating flag epochs for ${INSTANCE_ID}/${TEAM_ID}"
    while true; do
      PGPASSWORD="$DB_PASSWORD" psql -h "$DB_HOST" -p "$DB_PORT" \
        -U "$DB_USER" -d "$DB_NAME" -v ON_ERROR_STOP=1 -q -c \
        "SELECT vault.rotate('${INSTANCE_ID}', '${TEAM_ID}', ${FLAG_TTL_HOURS:-24});" \
        || echo "[!] rotation failed; will retry" >&2
      echo "[*] rotation complete; sleeping ${ROTATION_INTERVAL_SECONDS:-3600}s"
      sleep "${ROTATION_INTERVAL_SECONDS:-3600}"
    done
    ;;

  *)
    echo "[!] unknown HIDIMBI_ROLE '${ROLE}' (expected app|flagger|rotator)" >&2
    exit 2
    ;;
esac
