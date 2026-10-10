syntax=dockerfile-annotation-experimental:1

# ---------------------------------------------------------------------------
# Hidimbi's Town — one image, three roles.
#
#   HIDIMBI_ROLE=app     -> server.py            on :8000
#   HIDIMBI_ROLE=flagger -> flag_service.py      on :8001 and :8100
#   HIDIMBI_ROLE=rotator -> vault.rotate() only
#
# The image contains NO flag value and nothing that can be read to obtain one.
# The flag exists only as a row in the vault container's database.
#
# NOTE: the build context is the REPOSITORY ROOT (see docker-compose.yml),
# which is why every COPY path below is prefixed with backend/.
# ---------------------------------------------------------------------------
FROM python:3.11-slim

ENV PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    HIDIMBI_ROLE=app

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
      openssl curl postgresql-client procps \
    && rm -rf /var/lib/apt/lists/*

COPY backend/requirements.txt .
RUN pip install -r requirements.txt

# --- application code -------------------------------------------------------
# config.json is the repository ROOT's file: chain material only, never a flag.
COPY backend/server.py backend/flag_service.py backend/db_access.py \
     backend/chain_selector.py ./
COPY config.json ./
COPY backend/private_key.pem backend/public_key.pem ./

# --- provisioning helpers (not needed at runtime, useful for debugging) ------
COPY backend/provision_vault.py .
COPY backend/db ./db

COPY backend/entrypoint.sh /app/
RUN chmod +x /app/entrypoint.sh

EXPOSE 8000 8001 8100

ENTRYPOINT ["/app/entrypoint.sh"]
