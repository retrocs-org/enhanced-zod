#!/bin/sh
# Refresh the Enhanced ZOD PHP vhost from GitHub main.
set -eu
APP=enhanced-zod
PARENT=${ZODEE_DEPLOY_PARENT:-/srv/www/vhosts}
SITE="$PARENT/$APP.retrocs.org"
CONFIG="$SITE/config.inc.php"
REPOSITORY=${ZODEE_REPOSITORY:-https://github.com/retrocs-org/enhanced-zod.git}
LOCK="$PARENT/.refresh-$APP.lock"

fail() { printf '%s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || fail 'Run as root.'
for program in git php getent flock; do
    command -v "$program" >/dev/null 2>&1 || fail "Missing command: $program"
done
getent passwd wwwrun >/dev/null
getent group wwwrun >/dev/null
umask 077
[ -d "$PARENT" ] || fail "Missing vhost parent: $PARENT"
[ ! -L "$LOCK" ] || fail 'Refusing a symlink lock file.'
exec 9>>"$LOCK"
flock -n 9 || fail 'Another website refresh is running.'

check_live_config() {
    [ -d "$SITE" ] && [ ! -L "$SITE" ] && [ -f "$CONFIG" ] && [ ! -L "$CONFIG" ] ||
        fail "Expected an existing vhost with a regular config at $CONFIG."
}

check_live_config
TEMP=$(mktemp -d "$PARENT/.refresh-$APP.XXXXXX")
cleanup() {
    result=$?
    trap - 0 HUP INT TERM
    if [ -d "$TEMP/previous" ] && [ ! -e "$SITE" ] && [ ! -L "$SITE" ]; then
        if ! mv -- "$TEMP/previous" "$SITE"; then
            fail "Restore failed. Previous site retained at $TEMP/previous"
        fi
    fi
    rm -rf -- "$TEMP"
    exit "$result"
}
trap cleanup 0
trap 'exit 1' HUP INT TERM

GIT_TERMINAL_PROMPT=0 git clone --quiet --depth 1 --single-branch --branch main \
    --filter=blob:none --no-checkout -- "$REPOSITORY" "$TEMP/repository"
GIT_TERMINAL_PROMPT=0 git -C "$TEMP/repository" sparse-checkout set public-web
GIT_TERMINAL_PROMPT=0 git -C "$TEMP/repository" checkout --quiet main
REVISION=$(git -C "$TEMP/repository" rev-parse --verify HEAD)
SOURCE="$TEMP/repository/public-web"
for file in index.php config.inc.php .htaccess _app/src/service.php _app/src/bootstrap.php _app/src/views.php _app/bin/migrate.php; do
    [ -f "$SOURCE/$file" ] || fail "Website file missing: public-web/$file"
done
links=$(find "$SOURCE" -type l -print -quit)
[ -z "$links" ] || fail 'Website source contains symlinks.'

mkdir "$TEMP/new"
cp -R "$SOURCE/." "$TEMP/new/"
check_live_config
cp -p -- "$CONFIG" "$TEMP/new/config.inc.php"
php -r 'exit(str_starts_with(file_get_contents($argv[1]), "<?php") ? 0 : 1);' \
    "$TEMP/new/index.php" || fail 'Website index is missing its PHP opening tag.'

# Suppress lint diagnostics so a config syntax error cannot quote a secret.
find "$TEMP/new" -type f -name '*.php' -exec sh -c '
    for file do
        php -l "$file" >/dev/null 2>&1 || exit 1
    done
' sh {} + || fail 'PHP lint failed in the staged website, including its live config.'

chown -R wwwrun:wwwrun "$TEMP/new"
find "$TEMP/new" -type d -exec chmod 750 {} +
find "$TEMP/new" -type f -exec chmod 640 {} +
check_live_config
cmp -s "$CONFIG" "$TEMP/new/config.inc.php" || fail 'Live config changed during staging.'
mv -T -- "$SITE" "$TEMP/previous"
mv -T -- "$TEMP/new" "$SITE"
printf 'Updated %s from GitHub main at %s.\n' "$SITE" "$REVISION"
