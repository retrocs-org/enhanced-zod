#!/bin/sh
# Isolated, disposable MariaDB. Never uses the machine's regular database.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PHP_BIN=${PHP_BIN:-php}
export PHP_BIN
TASK_TMP=$(mktemp -d /tmp/zod-php.XXXXXX)
DB_PID=
cleanup() {
    if [ -n "$DB_PID" ]; then kill "$DB_PID" 2>/dev/null || true; wait "$DB_PID" 2>/dev/null || true; fi
    rm -rf "$TASK_TMP"
}
trap cleanup EXIT INT TERM
mariadb-install-db --no-defaults --datadir="$TASK_TMP/db" --auth-root-authentication-method=normal --skip-test-db >"$TASK_TMP/install.log" 2>&1
mariadbd --no-defaults --user="$(id -un)" --datadir="$TASK_TMP/db" --socket="$TASK_TMP/db.sock" --skip-networking --pid-file="$TASK_TMP/db.pid" --log-error="$TASK_TMP/db.log" >"$TASK_TMP/server.log" 2>&1 &
DB_PID=$!
ready=0
while [ "$ready" -lt 100 ]; do
    if mariadb-admin --no-defaults --socket="$TASK_TMP/db.sock" -uroot ping >/dev/null 2>&1; then break; fi
    ready=$((ready+1)); sleep .1
done
if [ "$ready" -eq 100 ]; then cat "$TASK_TMP/db.log" >&2; exit 1; fi
mariadb --no-defaults --socket="$TASK_TMP/db.sock" -uroot -e 'CREATE DATABASE zod_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci'
unset ZOD_DB_PASSWORD_FILE ZOD_TRUSTED_PROXIES
export ZOD_DB_SOCKET="$TASK_TMP/db.sock" ZOD_DB_NAME=zod_test ZOD_DB_USER=root ZOD_DB_PASSWORD=""
"$PHP_BIN" "$ROOT/public-web/_app/bin/migrate.php"
python3 "$ROOT/tests/network/php_api.py"
if [ "$#" -eq 3 ]; then
    mariadb --no-defaults --socket="$TASK_TMP/db.sock" -uroot zod_test -e "DELETE FROM rate_limits"
    python3 "$ROOT/tests/network/integration.py" "$PHP_BIN" "$1" "$2" "$3" "$ROOT"
elif [ "$#" -ne 0 ]; then
    echo "Usage: run-local.sh [agent-binary engine-binary engine-regression-driver]" >&2
    exit 1
fi
