#!/bin/sh
# Deploy the self-contained public-web directory directly into the vhost.
set -eu
APP=zodee
PARENT=${ZODEE_DEPLOY_PARENT:-/srv/www/vhosts}
SITE="$PARENT/$APP.retrocs.org"
REPOSITORY=${ZODEE_REPOSITORY:-https://github.com/retrocs-org/zodee.git}
[ "$(id -u)" -eq 0 ] || { echo "Run as root." >&2; exit 1; }
for program in git php getent; do
    command -v "$program" >/dev/null 2>&1 || { echo "Missing command: $program" >&2; exit 1; }
done
getent passwd wwwrun >/dev/null
getent group wwwrun >/dev/null
[ ! -L "$SITE" ] || { echo "Refusing a symlink document root." >&2; exit 1; }
[ ! -e "$SITE" ] || [ -d "$SITE" ] || { echo "Document root is not a directory." >&2; exit 1; }
mkdir -p "$PARENT"
TEMP=$(mktemp -d "$PARENT/.refresh-$APP.XXXXXX")
COMPLETE=0
OLD_MOVED=0
SITE_INSTALLED=0
cleanup() {
    result=$?
    trap - 0 HUP INT TERM
    if [ "$COMPLETE" -eq 0 ]; then
        if [ "$SITE_INSTALLED" -eq 1 ]; then rm -rf -- "$SITE"; fi
        if [ "$OLD_MOVED" -eq 1 ]; then
            if ! mv "$TEMP/previous" "$SITE"; then
                echo "Restore failed. Previous site retained at $TEMP/previous" >&2
                exit 1
            fi
        fi
    fi
    rm -rf -- "$TEMP"
    exit "$result"
}
trap cleanup 0
trap 'exit 1' HUP INT TERM
git clone --quiet --depth 1 --single-branch --branch main "$REPOSITORY" "$TEMP/repository"
SOURCE="$TEMP/repository/public-web"
for file in index.php _app/src/service.php _app/src/bootstrap.php _app/src/views.php _app/bin/migrate.php; do
    [ -f "$SOURCE/$file" ] || { echo "Website file missing: public-web/$file; site left unchanged." >&2; exit 1; }
done
[ -z "$(find "$SOURCE" -type l -print)" ] || { echo "Linked website files are unsupported; site left unchanged." >&2; exit 1; }
# php -l accepts plain text, so also verify that index.php really starts PHP.
php -r '$s=file_get_contents($argv[1]); if (!str_starts_with($s,"<?php")) {fwrite(STDERR,"Missing PHP opening tag; site left unchanged.\n");exit(1);}' "$SOURCE/index.php"
find "$SOURCE" -type f -name '*.php' -exec sh -c '
    marker=$1
    shift
    for file do
        php -l "$file" >/dev/null || { touch "$marker"; exit 1; }
    done
' sh "$TEMP/lint-failed" {} +
[ ! -e "$TEMP/lint-failed" ] || { echo "PHP lint failed; site left unchanged." >&2; exit 1; }
mkdir "$TEMP/new-site"
cp -R "$SOURCE/." "$TEMP/new-site/"
chown -R root:wwwrun "$TEMP/new-site"
find "$TEMP/new-site" -type d -exec chmod 750 {} +
find "$TEMP/new-site" -type f -exec chmod 640 {} +
[ ! -L "$SITE" ] || { echo "Document root became a symlink; refusing update." >&2; exit 1; }
if [ -e "$SITE" ]; then
    mv "$SITE" "$TEMP/previous"
    OLD_MOVED=1
fi
mv "$TEMP/new-site" "$SITE"
SITE_INSTALLED=1
COMPLETE=1
COMMIT=$(git -C "$TEMP/repository" rev-parse --verify HEAD)
printf 'Updated %s directly from public-web (%s).\n' "$SITE" "$COMMIT"
