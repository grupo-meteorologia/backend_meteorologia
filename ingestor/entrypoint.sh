#!/usr/bin/env bash
set -euo pipefail

DB_PATH="/data/dev.db"

if [ ! -f /data/dev.db ]; then
  sqlite3 "$DB_PATH" < /app/database/schema.sql
fi

exec "$@"
