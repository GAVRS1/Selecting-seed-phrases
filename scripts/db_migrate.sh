#!/usr/bin/env bash
set -euo pipefail

ENV_FILE="${1:-.env}"
MIGRATIONS_DIR="${MIGRATIONS_DIR:-migrations}"

if [[ -f "$ENV_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a
fi

if [[ -z "${RECOVERY_POSTGRES_CONN:-}" ]]; then
  echo "RECOVERY_POSTGRES_CONN is empty. Set it in $ENV_FILE or in the environment." >&2
  exit 1
fi

if [[ ! -d "$MIGRATIONS_DIR" ]]; then
  echo "Migrations directory not found: $MIGRATIONS_DIR" >&2
  exit 1
fi

psql "$RECOVERY_POSTGRES_CONN" -v ON_ERROR_STOP=1 -q -c \
  "CREATE TABLE IF NOT EXISTS schema_migrations (filename TEXT PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT NOW());"

while IFS= read -r migration_file; do
  migration_name="$(basename "$migration_file")"
  already_applied="$(psql "$RECOVERY_POSTGRES_CONN" -v ON_ERROR_STOP=1 -tA -c \"SELECT 1 FROM schema_migrations WHERE filename = '$migration_name' LIMIT 1;\")"
  if [[ "$already_applied" == "1" ]]; then
    echo "skip $migration_name"
    continue
  fi

  echo "apply $migration_name"
  psql "$RECOVERY_POSTGRES_CONN" -v ON_ERROR_STOP=1 -f "$migration_file"
  psql "$RECOVERY_POSTGRES_CONN" -v ON_ERROR_STOP=1 -q -c \
    "INSERT INTO schema_migrations(filename) VALUES ('$migration_name');"
done < <(find "$MIGRATIONS_DIR" -maxdepth 1 -type f -name '*.sql' | sort)

echo "Migrations are up to date."
