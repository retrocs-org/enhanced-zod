#!/bin/sh
# Refresh the PHP website while retaining the existing public document root.
set -eu
APP=zodee
PARENT=${ZODEE_DEPLOY_PARENT:-/srv/www/vhosts}
SITE="$PARENT/$APP.retrocs.org"
PRIVATE="$PARENT/.$APP-application"
REPOSITORY=${ZODEE_REPOSITORY:-https://github.com/retrocs-org/zodee.git}
[ "$(id -u)" -eq 0 ] || { echo "Run as root." >&2; exit 1; }
for program in git php getent; do
    command -v "$program" >/dev/null 2>&1 || { echo "Missing command: $program" >&2; exit 1; }
done
getent passwd wwwrun >/dev/null
getent group wwwrun >/dev/null
for directory in "$SITE" "$PRIVATE"; do
    [ ! -L "$directory" ] || { echo "Refusing a symlink deployment directory: $directory" >&2; exit 1; }
    [ ! -e "$directory" ] || [ -d "$directory" ] || { echo "Not a directory: $directory" >&2; exit 1; }
done
mkdir -p "$PARENT"
TEMP=$(mktemp -d "$PARENT/.refresh-$APP.XXXXXX")
COMPLETE=0
OLD_MOVED=0
SITE_INSTALLED=0
RELEASE_INSTALLED=0
RELEASE=
cleanup() {
    result=$?
    trap - 0 HUP INT TERM
    if [ "$COMPLETE" -eq 0 ]; then
        if [ "$SITE_INSTALLED" -eq 1 ]; then rm -rf -- "$SITE"; fi
        if [ "$OLD_MOVED" -eq 1 ]; then
            if ! mv "$TEMP/previous" "$SITE"; then
                echo "Restore failed. Recovery trees retained at $TEMP and $RELEASE" >&2
                exit 1
            fi
        fi
        if [ "$RELEASE_INSTALLED" -eq 1 ]; then rm -rf -- "$RELEASE"; fi
    fi
    rm -rf -- "$TEMP"
    exit "$result"
}
trap cleanup 0
trap 'exit 1' HUP INT TERM
git clone --quiet --depth 1 --single-branch --branch main "$REPOSITORY" "$TEMP/repository"
SOURCE="$TEMP/repository"
for file in public-web/index.php web-service/php/src/service.php web-service/php/src/bootstrap.php web-service/php/src/views.php web-service/php/bin/migrate.php; do
    [ -f "$SOURCE/$file" ] || { echo "Website file missing: $file; site left unchanged." >&2; exit 1; }
done
[ -z "$(find "$SOURCE/public-web" "$SOURCE/web-service/php" -type l -print)" ] || { echo "Linked application files are unsupported; site left unchanged." >&2; exit 1; }
COMMIT=$(git -C "$TEMP/repository" rev-parse --verify HEAD)
RELEASE="$PRIVATE/release-$COMMIT-${TEMP##*.}"
find "$SOURCE/public-web" "$SOURCE/web-service/php" -type f -name '*.php' -exec sh -c '
    marker=$1
    shift
    for file do
        php -l "$file" >/dev/null || { touch "$marker"; exit 1; }
    done
' sh "$TEMP/lint-failed" {} +
[ ! -e "$TEMP/lint-failed" ] || { echo "PHP lint failed; site left unchanged." >&2; exit 1; }
mkdir "$TEMP/new-app" "$TEMP/new-site"
mkdir -p "$TEMP/new-app/web-service"
cp -R "$SOURCE/web-service/php" "$TEMP/new-app/web-service/php"
cp -R "$SOURCE/public-web" "$TEMP/new-app/public-web"
cp -R "$SOURCE/public-web/." "$TEMP/new-site/"
# The application index runs from its own directory, so the backend stays private.
# Escape the configured filesystem path as a PHP literal rather than shell text.
php -r 'echo "<?php\ndeclare(strict_types=1);\nrequire ", var_export($argv[1]."/public-web/index.php", true), ";\n";' "$RELEASE" > "$TEMP/new-site/index.php"
php -l "$TEMP/new-site/index.php" >/dev/null
chown -R root:wwwrun "$TEMP/new-app" "$TEMP/new-site"
find "$TEMP/new-app" "$TEMP/new-site" -type d -exec chmod 750 {} +
find "$TEMP/new-app" "$TEMP/new-site" -type f -exec chmod 640 {} +
mkdir -p "$PRIVATE"
chown root:wwwrun "$PRIVATE"
chmod 750 "$PRIVATE"
# Each public entry point references an immutable release; the old site keeps
# using its previous application until the public directory is switched.
[ ! -L "$SITE" ] && [ ! -L "$PRIVATE" ] || { echo "Deployment directory became a symlink; refusing update." >&2; exit 1; }
mv "$TEMP/new-app" "$RELEASE"
RELEASE_INSTALLED=1
if [ -e "$SITE" ]; then
    mv "$SITE" "$TEMP/previous"
    OLD_MOVED=1
fi
mv "$TEMP/new-site" "$SITE"
SITE_INSTALLED=1
COMPLETE=1
printf 'Updated %s from main (%s).\nPrivate application: %s\n' "$SITE" "$COMMIT" "$RELEASE"
