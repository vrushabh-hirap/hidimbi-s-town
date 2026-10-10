-- ---------------------------------------------------------------------------
-- 00_create_roles.sql — create the three least-privilege roles.
--
-- Runs FIRST (alphabetical initdb order), because 01_schema.sql and the grants
-- in 10_provision.sh both reference these roles. No passwords are set here:
-- 10_provision.sh assigns them from the platform-injected environment, and
-- they are never committed.
-- ---------------------------------------------------------------------------

DO $$
DECLARE
    r record;
BEGIN
    FOR r IN SELECT * FROM (VALUES
        ('vault_owner',   'own'),
        ('vault_flagger', 'r'),
        ('vault_rotator', 'rw')
    ) AS t(role_name, mode)
    LOOP
        IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = r.role_name) THEN
            EXECUTE format('CREATE ROLE %I', r.role_name);
        END IF;
    END LOOP;
END
$$;
