# zod-ee website, PHP API and MariaDB

The central Zod service is **PHP + MariaDB**. The earlier Go/JSON hub has been
replaced. The small Go **dedicated-server agent** remains a separate client of
the PHP API; it never stores website accounts or statistics.

Everything is in this repository. There is no OpenFP code, account database,
service, domain or deployment dependency.

## Website

`php/public/` is the document root; `php/src/`, `php/sql/` and `php/bin/` must
remain outside the public root.

* **Overview:** project introduction, real game imagery and live network totals.
* **Servers:** searchable public directory, lobby state, addresses and join tickets.
* **Leaderboard:** all-time unranked wins, matches, units destroyed and captures.
* **Statistics:** network totals, map popularity, average duration and recent battles.
* **Player profiles:** a commander's statistics and recent match history.
* **Account:** registration, sign-in/out, dashboard and password change.
* **Downloads / field guide:** platform releases, building, joining and hosting.

The responsive design uses local assets and fonts, no CDN or third-party
analytics. Empty lists show honest empty states. Release buttons appear only
when an operator configures real downloads. Account forms work without
JavaScript; the join-ticket dialog uses a small same-origin script.

## Requirements and configuration

* PHP 8.4+ with **PDO MySQL**, **mbstring**, and **Argon2id** password support.
* MariaDB 10.6+ (Docker deployment uses MariaDB 11.4).
* Apache with `FallbackResource` or an equivalent PHP-FPM front controller.
* HTTPS for production. No Composer packages or Go runtime are needed for the site.

| Environment variable | Purpose |
|---|---|
| `ZOD_DB_HOST`, `ZOD_DB_PORT` | MariaDB host and port (defaults `127.0.0.1:3306`) |
| `ZOD_DB_SOCKET` | Optional local Unix socket, takes precedence over host/port |
| `ZOD_DB_NAME`, `ZOD_DB_USER` | Database and application user (defaults `zod`) |
| `ZOD_DB_PASSWORD` or `ZOD_DB_PASSWORD_FILE` | Database secret; file option is preferred |
| `ZOD_PUBLIC_URL` | Canonical origin, e.g. `https://zod.example.com` |
| `ZOD_TRUSTED_PROXIES` | Explicit trusted proxy IP/CIDR list; empty by default |
| `ZOD_DEV_HTTP=1` | Development only: insecure cookies for loopback requests |
| `ZOD_DOWNLOADS_FILE` | Optional private JSON file describing published releases |

Use a dedicated database/user. The application user needs schema DML permissions;
run migrations with a user allowed to create/alter tables. Set the same variables
for CLI administration and web requests. Never expose MariaDB to game clients.

```sh
# Create the database/user using your MariaDB administration tools, then:
export ZOD_DB_HOST=127.0.0.1 ZOD_DB_NAME=zod ZOD_DB_USER=zod
export ZOD_DB_PASSWORD_FILE=/private/zod-db-password
php web-service/php/bin/migrate.php
```

For local development, after creating a disposable database:

```sh
ZOD_DEV_HTTP=1 ZOD_PUBLIC_URL=http://127.0.0.1:8088 \
  php -S 127.0.0.1:8088 -t web-service/php/public web-service/php/public/router.php
```

PHP's built-in web server is for development, not deployment. Configure Apache's
document root to `web-service/php/public`; `packaging/linux/apache.conf` is the
container vhost. `packaging/linux/nginx.conf.example` is the HTTPS reverse-proxy
example. The proxy must **overwrite** `X-Real-IP`; trust only its actual IP/CIDR,
never all callers. Forward `Authorization` to PHP (the Apache example does this).

## Docker deployment

```sh
mkdir -p packaging/linux/secrets
chmod 700 packaging/linux/secrets
openssl rand -hex 32 > packaging/linux/secrets/db-password
openssl rand -hex 32 > packaging/linux/secrets/db-root-password
chmod 600 packaging/linux/secrets/*
export ZOD_PUBLIC_URL=https://YOUR-ZOD-WEBSITE

docker compose -f packaging/linux/compose.web.yaml up --build -d
```

This starts PHP/Apache and MariaDB. Only host loopback port 8088 is published;
MariaDB has no published port. A named volume holds the database. The initial
schema is installed on a **new** volume; apply `bin/migrate.php` explicitly for
existing deployments. The compose network has a fixed gateway trusted by the
application for the host-local HTTPS proxy. Adjust that subnet/trusted IP together
if it conflicts with your host's networks.

Place a TLS reverse proxy on the host in front of `127.0.0.1:8088`. Do not publish
account endpoints over public HTTP. The optional GET-only `/matches` proxy route
supports the current native browser's HTTP discovery while credentials stay on
HTTPS. The legacy process-spawning orchestrator is unrelated and must not be
exposed as this public API.

Back up MariaDB with `mariadb-dump` or your normal transactional database tooling.
Database credentials, backups, server keys, and downloaded release archives must
not be committed. `packaging/linux/secrets/` is ignored by Git and Docker context.

Run `php web-service/php/bin/cleanup.php` periodically (for example hourly via
cron) to remove expired sessions, tickets, grants, stale listings and rate limits.
Expiry is enforced in queries even when the cleanup job has not run. Match
history and account statistics are retained.

## Public server provisioning

Generate a different key for every approved server:

```sh
openssl rand -hex 32 > /private/zod-server-key
chmod 600 /private/zod-server-key
php web-service/php/bin/server.php --id europe-1 --name 'Europe' \
  --key-file /private/zod-server-key
```

The database stores a SHA-256 hash, not the server's plaintext key. Give the key
file only to that server operator. Run the CLI inside the web container with its
DB environment configured, or from a trusted host with DB access. For Docker,
mount the key file temporarily into that container; do not put it on a command
line or in a public directory. Disable an operator key and withdraw its listing:

```sh
php web-service/php/bin/server.php --id europe-1 --disable
```

The same provisioning command rotates a key. Current server agents use bearer
authentication and work with the PHP API unchanged.

## Private and public dedicated servers

Build only the Go agent:

```sh
go -C web-service build -o ../build-web/zod-agent ./cmd/zod-agent
go -C web-service test -race ./...
```

Build the engine with CMake, or use the Linux server package. It initializes no
video/audio device in `-d` mode and requires no X server, Wayland session or GPU.

```sh
# Private: no listing, no cloud dependency, guests can play.
./build-web/zod-agent --mode private --engine "$PWD/build/zod-ee" \
  --data "$PWD" --state "$PWD/build-private-state"

# Public: approved key, listing and required cloud authentication.
./build-web/zod-agent --mode public --engine "$PWD/build/zod-ee" \
  --data "$PWD" --state "$PWD/build-public-state" \
  --hub https://YOUR-ZOD-WEBSITE --id europe-1 --key-file /private/zod-server-key
```

The agent announces every 20 seconds; listings expire after 90 seconds without a
heartbeat. Its public IPv4 address must match the address observed by the API.
Leave `--public-ip` unset for normal NAT hosting. Open/forward the game TCP port
(2137 by default). Private servers do not announce and allow guests, including
when the website is unavailable. Public servers refuse new human logins if cloud
verification fails; existing verified players can finish their round. They never
silently switch into guest mode. Spectators also authenticate on public servers.

Use one `--state` directory per instance. Engine flags follow `--`, e.g.
`-- -b blue -D 2`. Completed rounds return to a fresh ready-up lobby. Public match
participants are fixed at round start; late arrivals spectate until the next
round. Bot authentication remains separate and loopback-only.

Docker examples: `packaging/linux/compose.private.yaml` and
`packaging/linux/compose.public.yaml`. Native systemd example:
`packaging/linux/zod-dedicated.service`. The root Dockerfile's `dedicated-runtime`
target builds the private-by-default server; `web-runtime` builds PHP only.

Linux server packaging:

```sh
python3 packaging/linux/package.py --engine build/zod-ee \
  --agent build-web/zod-agent --out build/zod-server-linux.tar.gz
```

The package includes engine runtime libraries, the Go agent, and the PHP website
source under `website/`. Host that website with PHP/MariaDB separately; there is
no longer a Go `zod-web` executable.

## Joining and statistics

Sign in at `/account`, open `/servers`, and choose **Join**. The dialog issues a
server-bound, single-use ticket that expires in two minutes. It gives macOS/Linux
and Windows PowerShell commands; replace the executable path with your game path.
The game sends the ticket, never your password, to the server. Accounts are
case-insensitive; public names come from the account. One account may hold one
verified connection per public server.

The server simulation records kills, unit losses and zone captures. Completed
reports are atomically spooled and uploaded by the agent. The PHP API verifies
server credentials and per-player grants and commits each match and all its
players in one MariaDB transaction. Identical retries do not duplicate results;
conflicting retries are rejected. Clients cannot choose arbitrary accounts to
credit. Only approved operators should receive keys: a modified trusted server
can still falsify its simulation. Statistics are unranked; bot matches count.
Private games never upload statistics.

Passwords use Argon2id. Sessions last 24 hours and are stored hashed. Cookies are
Secure, HttpOnly and SameSite=Strict by default. Form actions have CSRF protection;
JSON mutations check origin and content type. Login/registration attempts are
rate-limited in MariaDB across PHP workers. Password changes rotate the current
session and revoke other website sessions. Existing game connections/grants are
unaffected; connection grants last 24 hours, so reconnect for longer sessions.

Native in-game account screens, email verification/recovery, account deletion
and encrypted game transport remain follow-up work. The current game TCP
connection is unencrypted, so use a trusted network path and keep join tickets
private. Website/API credentials belong on HTTPS only.

## Migrating the previous JSON hub

Stop the old hub and back up its JSON file. Create an empty MariaDB schema, then:

```sh
# First inspect record counts; no writes by default.
php web-service/php/bin/import-json.php --file /backup/zod.json

# Preserve accounts, IDs, active sessions/tickets/grants, and complete results.
php web-service/php/bin/import-json.php --file /backup/zod.json \
  --keys-json /private/old-server-keys.json --commit
```

The import is transactional, refuses a non-empty account/match database, and does
not modify the source file. Expired credentials are skipped. Legacy PBKDF2 password
hashes are verified as before and upgraded to Argon2id on successful sign-in; no
password reset is needed. Server keys are imported as hashes. Without a key file,
legacy credential references remain disabled until the operator provisions keys.
Transient live listings are not imported; agents announce again after deployment.

## Release downloads

Optionally set `ZOD_DOWNLOADS_FILE` to a private file such as:

```json
{
  "windows": {"version":"YOUR-VERSION", "url":"/downloads/zod-windows.zip"},
  "macos": {"version":"YOUR-VERSION", "url":"https://YOUR-HOST/zod.dmg"},
  "linux": {"version":"YOUR-VERSION", "url":"/downloads/zod-linux.tar.gz"}
}
```

Publish those files separately. Without this configuration, the website shows
source-build links instead of fake or broken release buttons.

## API compatibility

| Method / endpoint | Purpose |
|---|---|
| `POST /api/accounts` | Register username/password |
| `POST /api/login`, `POST /api/logout` | Create/revoke session |
| `GET /api/me` | Current account |
| `POST /api/account/password` | Change password using current password |
| `GET /api/servers`, `GET /matches` | Live public directory |
| `POST /api/servers/heartbeat`, `DELETE /api/servers/self` | Approved server listing |
| `POST /api/tickets`, `POST /api/tickets/redeem` | Issue/consume join ticket |
| `POST /api/results` | Transactional authenticated match report |
| `GET /api/leaderboard`, `GET /api/statistics` | Public records |
| `GET /healthz` | Database/schema health |

JSON request field names retain compatibility with the Go agent and existing
clients. Programmatic sessions and approved servers use `Authorization: Bearer`.
Passwords, keys and session/ticket credentials must not be placed in URLs or logs.

## Tests

Configure an **isolated empty MariaDB database**, migrate it, then run:

```sh
python3 web-service/tests/php_api.py
python3 web-service/tests/integration.py php "$PWD/build-web/zod-agent" \
  "$PWD/build/zod-ee" "$PWD/build/zod_engine_regressions" "$PWD"
go -C web-service test -race ./...
```

The PHP tests cover schema/import compatibility, Argon2id upgrade, concurrent
single-use ticket redemption, cross-server rejection, transactional rollback,
concurrent duplicate reports, stale listings, session rotation, CSRF, origin
checks and the website pages. The engine integration launches the PHP service,
agent and engine and checks public rejection, verified names/world download, bot
startup, native listing parsing, result upload, service outages and private guests.

## Production site

Canonical URL: `https://zodee.retrocs.org` (`ZOD_PUBLIC_URL`).
Web host: `sth-ts-web003.kladhest.se`; database host: `sth-ts-dbs001.kladhest.se`.
Existing MariaDB database/user names remain independently configurable through
`ZOD_DB_NAME` and `ZOD_DB_USER`; the project rename does not migrate database accounts.
Project source: https://gitlab.kladhest.se/retrocs-org/zodee
and https://github.com/retrocs-org/zodee.

Before deploying a source checkout, run `python3 tools/bootstrap_fonts.py` from
the repository root to populate the ignored, licensed UI font dependencies.
The website illustration is project-authored SVG; no original game screenshot
or other game media is committed with the website.
