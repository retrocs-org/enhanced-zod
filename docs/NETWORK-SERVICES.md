# zod-enhanced website, PHP API and MariaDB

The central Zod service is **PHP + MariaDB**. The earlier Go/JSON hub has been
replaced. The small Go **dedicated-server agent** in `server-agent/` remains a separate client of
the PHP API; it never stores website accounts or statistics.

Everything is in this repository. There is no OpenFP code, account database,
service, domain or deployment dependency.

## Website

`public-web/` is the complete, self-contained PHP website and document root.
Internal code, CLI tools and guarded migrations live in its `_app/` directory.
Deny HTTP access to `_app/` and `config.inc.php`. Configure database and service
settings in the installed `config.inc.php`; environment variables override it.

* **Overview:** project introduction and live network totals.
* **Network services:** integrated service overview, live counts, server directory and leaderboard links.
* **Features:** a project timeline and highlights of multiplayer, community services, editors and modern desktop support.
* **API browser:** developer tooling available by direct URL, hidden from public navigation.
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

## Configuration file

`public-web/config.inc.php` contains database host, port, name, user, password
(or password-file path), socket, public URL, development HTTP, trusted proxies
and download-manifest settings. Edit the deployed copy at
`/srv/www/vhosts/zodee.retrocs.org/config.inc.php`. The refresh script preserves
that copy and checks its PHP syntax before replacing the website.

The repository copy has the correct private database host and website URL,
with an empty password. Set the generated application password locally; never
commit production secrets. All values are strings. Environment variables take
precedence so Docker and existing Apache/PHP configurations continue to work.
An environment password-file setting wins over an environment password value;
a configured password-file setting wins over the password in the config file.
The same configuration is loaded by the API, website and CLI administration.

After configuration, initialize tables with:

```sh
cd /srv/www/vhosts/zodee.retrocs.org
php _app/bin/migrate.php
```

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
php public-web/_app/bin/migrate.php
```

For local development, after creating a disposable database:

```sh
ZOD_DEV_HTTP=1 ZOD_PUBLIC_URL=http://127.0.0.1:8088 \
  php -S 127.0.0.1:8088 -t public-web public-web/router.php
```

PHP's built-in web server is for development, not deployment. Configure Apache's
document root to `public-web`; `packaging/linux/apache.conf` is the
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

Run `php public-web/_app/bin/cleanup.php` periodically (for example hourly via
cron) to remove expired sessions, tickets, grants, stale listings and rate limits.
Expiry is enforced in queries even when the cleanup job has not run. Match
history and account statistics are retained.

## Public server provisioning

Generate a different key for every approved server:

```sh
openssl rand -hex 32 > /private/zod-server-key
chmod 600 /private/zod-server-key
php public-web/_app/bin/server.php --id europe-1 --name 'Europe' \
  --key-file /private/zod-server-key
```

The database stores a SHA-256 hash, not the server's plaintext key. Give the key
file only to that server operator. Run the CLI inside the web container with its
DB environment configured, or from a trusted host with DB access. For Docker,
mount the key file temporarily into that container; do not put it on a command
line or in a public directory. Disable an operator key and withdraw its listing:

```sh
php public-web/_app/bin/server.php --id europe-1 --disable
```

The same provisioning command rotates a key. Current server agents use bearer
authentication and work with the PHP API unchanged.

## Private and public dedicated servers

Build only the Go agent:

```sh
go -C server-agent build -o ../build-web/zod-agent ./cmd/zod-agent
go -C server-agent test -race ./...
```

Build the engine with CMake, or use the Linux server package. It initializes no
video/audio device in `-d` mode and requires no X server, Wayland session or GPU.

```sh
# Private: no listing, no cloud dependency, guests can play.
./build-web/zod-agent --mode private --engine "$PWD/build/zod-enhanced" \
  --data "$PWD" --state "$PWD/build-private-state"

# Public: sign in as the cloud owner; credentials persist in this state directory.
./build-web/zod-agent --mode public --engine "$PWD/build/zod-enhanced" \
  --data "$PWD" --state "$PWD/build-public-state" \
  --hub https://zodee.retrocs.org/index.php --id europe-1 --login
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
python3 packaging/linux/package.py --engine build/zod-enhanced \
  --agent build-web/zod-agent --out build/zod-server-linux.tar.gz
```

The package includes engine runtime libraries, the Go agent, and the PHP website
source under `public-web/` with its private backend under `public-web/_app/`. Host that website with PHP/MariaDB separately; there is
no longer a Go `zod-web` executable.

## Joining and statistics

In the native Multiplayer browser, choose **Online games**, select a server and
join. The game opens the portal in your browser. Sign in, check the server and
request code, and approve. The game receives a server-bound, single-use ticket
and connects automatically. Passwords and website sessions stay in the browser;
the device secret and two-minute join ticket remain in memory. Back/close cancels
a pending request; requests expire after five minutes. Accounts are case-insensitive;
public names come from the account. One account may hold one verified connection
per public server. The website's manual ticket commands remain available.

The default directory is `https://zodee.retrocs.org/index.php`. Set
`ZOD_PORTAL_URL` to use another HTTPS portal. Local development alone may set
`ZOD_PORTAL_DEV_HTTP=1` with a loopback HTTP URL. TLS verifies the certificate and
hostname; redirects are rejected. Builds copy public system CA roots into
`assets/portal-ca.crt` when available. `-DZOD_CA_BUNDLE=/path/to/ca.pem` selects a
build-time bundle; `ZOD_PORTAL_CA_FILE` selects a runtime trust file. These files
contain public trust roots and are generated packaging inputs, not game media.

Hosting from the game stays local by default. `ZOD_ORCHESTRATOR=local` explicitly
selects local games; an explicit `ZOD_ORCHESTRATOR=host:port` retains the legacy
HTTP development service. It is separate from the HTTPS public portal.

The server simulation records kills, unit losses and zone captures. Completed
reports are atomically spooled and uploaded by the agent. The PHP API verifies
server credentials and per-player grants and commits each match and all its
players in one MariaDB transaction. Identical retries do not duplicate results;
conflicting retries are rejected. Clients cannot choose arbitrary accounts to
credit. Public hosts are account-owned, but a modified host can still falsify
its simulation. These records are community statistics, not a trusted ranking. Statistics are unranked; bot matches count.
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
php public-web/_app/bin/import-json.php --file /backup/zod.json

# Preserve accounts, IDs, active sessions/tickets/grants, and complete results.
php public-web/_app/bin/import-json.php --file /backup/zod.json \
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
python3 tests/network/php_api.py
python3 tests/network/integration.py php "$PWD/build-web/zod-agent" \
  "$PWD/build/zod-enhanced" "$PWD/build/zod_engine_regressions" "$PWD"
go -C server-agent test -race ./...
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
Project source: https://github.com/retrocs-org/zodee.

Before deploying a source checkout, run `python3 tools/bootstrap_fonts.py` from
the repository root to populate the ignored, licensed UI font dependencies.
The website illustration is project-authored SVG; no original game screenshot
or other game media is committed with the website.

## Deploying the repository website

Copy `public-web/.` directly into `/srv/www/vhosts/zodee.retrocs.org/`, including
its dotfiles and `_app/` directory. The real `index.php` begins with `<?php` and
loads the application from `__DIR__.'/_app/'`. No wrapper, external release
directory or sibling repository files are used.

As root on sth-ts-web003:

```sh
curl -fsSL https://raw.githubusercontent.com/retrocs-org/zodee/main/packaging/linux/refresh-zodee.sh -o /srv/www/vhosts/refresh-zodee.sh
sh -n /srv/www/vhosts/refresh-zodee.sh
sh /srv/www/vhosts/refresh-zodee.sh
```

The script stages only `public-web`, verifies the opening PHP tag and all PHP
syntax, then replaces the vhost tree with rollback on failure. It does not modify
databases, credentials or existing legacy `.zodee-application` releases.

Apache must execute PHP, use `DirectoryIndex index.php`, route application URLs
through `FallbackResource /index.php`, and deny HTTP access to `_app/`:

```apache
<Directory /srv/www/vhosts/zodee.retrocs.org>
    Options -Indexes -MultiViews
    DirectoryIndex index.php
    FallbackResource /index.php
    Require all granted
</Directory>
<Directory /srv/www/vhosts/zodee.retrocs.org/_app>
    Require all denied
</Directory>
SetEnvIf Authorization "(.*)" HTTP_AUTHORIZATION=$1
```

The bundled `.htaccess` files supply routing and internal-directory protection
when the vhost permits overrides. With `AllowOverride None`, use the explicit
vhost configuration above. Keep the database password file outside this tree.
If the page is blank, inspect Apache/PHP-FPM's error log and confirm that the
vhost PHP version is 8.4 or newer and PDO MySQL/mbstring are installed.

The network overview is `/network`; the API browser is `/api-browser` (`/api` is
an alias). `/api/catalog` provides the endpoint reference as JSON.

## Database host setup for zodee.retrocs.org

On `sth-ts-dbs001.kladhest.se` (`172.16.1.121`), run as the administrator whose
`.my.cnf` contains the existing MariaDB administrator login. No `-p` is needed:

```sh
ZODEE_DB_PASSWORD=$(openssl rand -hex 32)
mariadb <<SQL
CREATE DATABASE IF NOT EXISTS zodee CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'zodee'@'172.16.1.133' IDENTIFIED BY '$ZODEE_DB_PASSWORD';
GRANT ALL PRIVILEGES ON zodee.* TO 'zodee'@'172.16.1.133';
SHOW GRANTS FOR 'zodee'@'172.16.1.133';
SQL
printf 'Save this application password securely: %s\n' "$ZODEE_DB_PASSWORD"
unset ZODEE_DB_PASSWORD
```

This account accepts connections from `sth-ts-web003` (`172.16.1.133`) and only
has privileges on `zodee`. CREATE USER/GRANT take effect immediately; no FLUSH
PRIVILEGES is needed. If the account already exists, retain its password or use
ALTER USER explicitly rather than recreating it.

Allow TCP 3306 from `172.16.1.133` to `172.16.1.121`; MariaDB must listen on that
private interface. Configure these values for both PHP-FPM/Apache and CLI:

```sh
export ZOD_DB_HOST=172.16.1.121 ZOD_DB_PORT=3306
export ZOD_DB_NAME=zodee ZOD_DB_USER=zodee
export ZOD_DB_PASSWORD_FILE=/etc/zodee/db-password
export ZOD_PUBLIC_URL=https://zodee.retrocs.org
php /srv/www/vhosts/zodee.retrocs.org/_app/bin/migrate.php
```

Create `/etc/zodee/db-password` on the webserver with the generated application
password, readable by PHP's service account (`wwwrun`) but not other users.
Shell exports configure the migration command only; set the same variables in
the web service's persistent environment or Apache vhost before serving requests.
The administrator's `.my.cnf` remains only on the database host.

## Apache returns 404 for Network, Leaderboard or API

If `/` works but `/network` returns Apache's own Object not found page, PHP is
not receiving application routes. This is a routing problem, not evidence that
the database or leaderboard queries failed. By default the website now uses
`/index.php/network`, `/index.php/leaderboard` and `/index.php/api/...`, which
work without rewrite rules. Fixed page directories also accept existing clean
page bookmarks (Apache may add a trailing slash).

Set `ZOD_CLEAN_URLS` to `'1'` only after the vhost has working
`FallbackResource /index.php` (or equivalent rewriting). With overrides disabled,
configure that directive in the vhost itself rather than `.htaccess`. Existing
installed config files need no changes: absent `ZOD_CLEAN_URLS` defaults to
front-controller URLs. For dedicated servers, use the hub URL
`https://zodee.retrocs.org/index.php` until clean API URLs are routed by Apache.

The zod-enhanced website uses a Z-inspired industrial interface with beveled metal panels inspired by the game menus. It contains no gameplay
illustrations or simulated screenshots.

## Community portal upgrade

The expanded account, social and sharing workflows are documented in
[public-web/README.md](../public-web/README.md#community-features-and-database-upgrade).
Run the guarded migrations after deploying an update. Set PHP upload limits and
install ZipArchive for campaign submissions. Friends and presence are opt-in;
password recovery uses previously saved offline codes rather than email.


## Website moderation

Apply migration 004 with `php _app/bin/migrate.php` after copying `public-web`.
In the installed `config.inc.php`, set `ZOD_ADMIN_USERS` to a comma-separated list
of existing account usernames. The default is empty: nobody has operator access.
Protect those accounts with strong passwords and keep their recovery codes offline.
Refresh preserves the installed configuration, so add this new setting there explicitly.

Sign in and open `/index.php/admin` (or `/admin` with clean routing). Review each
revision's preview, files and redistribution permissions before approving it.
Operators can hide revisions, review private reports and enable/disable existing
server credentials. Key generation/provisioning stays in the server CLI: the
website never exposes secret keys. Disabling a server removes its listing and
revokes pending approvals/unused tickets; it does not kick already-connected players.
Actions are recorded with operator, target and UTC time. Public users can report
servers or published creations; reports are limited to ten per account per day.

These changes are tested locally; no production migration/deployment is performed.


## Public server ownership and administrators

Migration 005 makes a cloud owner mandatory for public hosting. Apply pending
migrations with `php _app/bin/migrate.php` and update the website, agent and engine
together. Existing unowned credentials cannot publish or accept cloud players
until claimed by an account. Campaign and private/LAN hosting remain account-free.

Start a public agent with `--login`, its normal `--engine`, `--data`, `--state`,
`--hub` and `--id` arguments. It prints a five-minute browser URL and code, which
also works over SSH on a headless server. Sign in to the portal, check the ID/code
and explicitly authorize the host. No cloud password or account session is entered
or saved on the dedicated machine. A new server ID is registered automatically.
For an unclaimed operator-provisioned ID, include its existing `--key-file` on
first authorization. Once ownership exists, that owner can authorize a replacement
host even without the old key; approval replaces the old host credential and
invalidates its listing and pending joins. Stale approvals cannot undo a newer
credential change. Possessing a host key alone cannot replace its owner.
Site-disabled servers remain disabled after owner sign-in.

The agent stores a server-scoped key in `--state/credentials/identity.json`, with a
private directory and mode 0600 file. Keep this state directory for restarts; the
same command subsequently runs without `--login` or `--key-file`. The saved identity
is bound to both hub URL and server ID. Do not share it, add it to Git, or put it in
a public document root. The portal stores credential hashes only. An interrupted
approval can be retried with the retained host key.

Sign in and open **My servers** (`/index.php/my-servers`). The owner automatically
has server administrator access and can add/remove existing cloud usernames.
Delegated administrators cannot change ownership, remove the owner or grant roles
themselves. All roles are scoped to one server and are separate from the website's
`ZOD_ADMIN_USERS` operator allowlist. Account ownership does not grant global site
moderation permissions. Administrator changes are recorded in the moderation audit.

Join the public game with the owner or delegated account. Server administrators
can directly pause/resume, restart/select maps, manage bots, reshuffle teams and
change allowed game speed. Regular players retain team selection and ready controls.
The engine validates roles rather than trusting client names or claimed flags.
Remote save/load and arbitrary filesystem map paths remain unavailable even to
public administrators. Private games keep their existing voting policy.

Roles refresh asynchronously every five seconds, with a fifteen-second validity
limit. Connected clients lose revoked permissions within about ten seconds; failed
refreshes suspend admin controls. A portal outage leaves the current game playable
but cannot grant/retain stale administrative authority indefinitely. New public
host startup and new player authentication fail closed when the portal is unavailable.


For unattended/system-service setup, use `--login-only` to authorize and exit
without launching a game, then start the agent normally using the same state path.
The packaged Compose deployment supports this one-time step:

```sh
cd packaging/linux
# Set ZOD_HUB_URL, ZOD_SERVER_ID and optionally ZOD_SERVER_NAME first.
docker compose -f compose.public.yaml -f compose.public.enroll.yaml run --rm dedicated
# After browser approval, normal restarts use the saved private host credential.
docker compose -f compose.public.yaml up -d
```

Existing operator-created keys remain usable for first sign-in with `--key-file`;
the agent copies that scoped credential into its private identity file. They do
not bypass the owner check, and a disabled server cannot re-enable itself.

## Public server operations

Deploy the website, engine and agent together and apply migration 006 with
`php _app/bin/migrate.php` from the deployed public-web directory. Back up the
database first. These changes do not run production migrations automatically.

My servers links to each server console. Owners manage hosting, credentials and
membership. Administrators configure matches and moderate players; moderators
can kick, temporarily ban and unban players but cannot change match settings.
Staff cannot moderate the owner or accounts of equal/higher rank. Bans last up to
seven days and apply to both existing connections and fresh authentication.
Kicks expire after sixty seconds if undelivered and show acknowledged delivery
status. Pending kicks recheck the actor's current permissions on the host.

Owners can disable/re-enable their listing or revoke its scoped host credential.
Revocation requires a fresh `--login` authorization. Neither action overrides a
site-operator disable. Existing gameplay may finish while cloud actions stop.
The console audit includes authorization, membership and portal operations.

Administrators select installed rotation maps and save named presets with bot
teams, difficulty, game speed and optional map voting. Settings apply in a paused
pre-start lobby; active rounds retain their settings. The host independently
checks every map against its installed selectable-map list. Add maps locally and
restart the host to refresh that list. No remote arbitrary filesystem paths are
accepted. Tactical previews show forts and territory boundaries, not terrain art.
Connected authenticated participants can vote at `/server-rotation?server=ID`
during the 25-second between-round window. One account has one replaceable vote;
the next map falls back to rotation order when no vote wins.

The agent reports health every five seconds: roster, simulation work per tick,
uptime, role-cache age, pending statistics and desired/applied configuration.
Reports older than thirty seconds are marked stale. Values are host-reported,
not an independent monitoring guarantee. Public disconnect recovery opens the
online browser with the same server selected and requests fresh browser approval;
it never reuses an already consumed join ticket.

## Match records, diagnostics and maintenance

Migration 007 adds team identifiers to new public match reports and scheduled
maintenance. Deploy website, agent and engine together, then run
`php _app/bin/migrate.php` in the deployed public-web directory. Existing match
reports remain readable; their team is shown as unknown. Retried pre-upgrade
reports keep their original canonical payload identity.

`/match-history` lists completed public matches; filter with `?server=SERVER_ID`
and open `?id=MATCH_ID` for participants, team, outcome, duration and statistics.
Profiles and server consoles link to those details. Privacy/friends/blocking rules
anonymize identities in shared match records. The API equivalents are
`GET /api/matches?server=ID&before=CURSOR` and `GET /api/match?id=ID`. No connection
grants or report payload hashes are exposed. Private games are never submitted.

During a public match, open **Player List** to moderate without a browser. Current
staff can select a player, enter a reason, then kick or apply a 60-minute ban.
The host and portal independently check current scoped roles and target rank.
Actions use the durable queue and audit log; revoking a role also invalidates
pending actions. The portal console supports other ban durations and unbanning.

Administrators/owners can schedule maintenance from the server console, thirty
seconds to one day ahead. The engine announces it, blocks a new round once due,
and lets an active round finish. Fresh cloud approvals stop at the deadline.
Choose Stop or Restart; cancelling is available before shutdown. A completed
schedule is acknowledged using its exact timestamp, so an old acknowledgement
cannot clear a newer schedule. A portal outage stops automatic maintenance
restart rather than skipping the acknowledgement. Pending results are flushed
best-effort and any unsent records remain in the private outbox.

Engine-version preflight explains mismatches before approval. The browser shows
the host map's SHA-256 and checks an installed local copy when available. A
different local map is not overwritten: the host's copy is downloaded. New hosts
send a SHA-256 for each map transfer, which is verified before parsing; malformed,
out-of-order and oversized transfers fail with recovery guidance. This detects
content mismatch/corruption, not an untrusted host's authenticity. Portal errors
distinguish DNS, timeout, certificate, routing, expired approval, denied access,
maintenance and service outages. Moderation disconnects retain their reason.

## Staging a dedicated-engine update

This workflow targets a standalone Linux dedicated engine with its normal system
libraries and an existing data installation. It does not replace the agent,
website, imported assets or operating-system dependencies. Obtain an engine built
for the same architecture and its SHA-256 through a trusted release channel.
The portal cannot upload binaries or execute arbitrary update commands.

As the local service account, stage the executable while the old engine runs:

```sh
zod-agent --state /var/lib/zod --data /opt/zod-enhanced \
  --map maps/p02_bb_orig01.map \
  --stage-engine /tmp/zod-enhanced-new --sha256 TRUSTED_RELEASE_SHA256
```

The agent checks the complete checksum, executes `--version` and `--map-info`,
then records the staged runtime in its private state. In the server console,
schedule **Restart / apply staged update**. Activation waits for the round boundary,
verifies the staged file again and retains a separate verified copy of the
previous engine. Startup failures, early unexpected exits or failure to report
ready within fifteen seconds restore the previous engine once. Failed versions
are not selected again automatically. Logs explain a rejected update/rollback.

For a manual rollback, stop the service first:

```sh
zod-agent --state /var/lib/zod --rollback-engine
# Start your normal dedicated-server service again.
```

Keep the state directory and its private permissions. Database/website upgrades
are a separate deployment step and require their own backup/rollback plan.
