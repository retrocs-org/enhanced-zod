# Self-contained PHP website

Website and API changes are tracked in [changelog.md](changelog.md).

Copy this entire directory, including dotfiles and `_app/`, into the vhost
document root. No sibling repository directory or generated PHP wrapper is needed.
The same PHP entry point handles the website, network services and JSON API.

`_app/` contains internal PHP code and CLI administration tools. Its `.htaccess`
denies HTTP access; the packaged Apache configuration also denies that directory
explicitly. Internal PHP files refuse direct HTTP execution. Schema migrations
are guarded PHP files rather than downloadable SQL. Database and website settings are in `config.inc.php`. Edit the installed copy;
refreshes preserve it. Environment variables override file settings. You can
keep the password in an external file using `ZOD_DB_PASSWORD_FILE` instead.

See [configuration and database setup](../docs/NETWORK-SERVICES.md). Local preview:

```sh
php -S 127.0.0.1:8088 -t public-web public-web/router.php
```

Production requires PHP 8.4+, PDO MySQL, mbstring, ZipArchive (for campaign sharing), MariaDB and Apache/PHP-FPM.
Route non-file requests to `/index.php` and deny HTTP access to `_app/`.

Configure `ZOD_DB_PASSWORD` in `/srv/www/vhosts/enhanced-zod.retrocs.org/config.inc.php`
with your application password, then run `php _app/bin/migrate.php` from the
vhost directory. The same configuration is used by website, API and CLI tools.
Do not commit your configured production file to Git.

Navigation and API-browser requests use `/index.php/...` by default, so they
work even if Apache ignores `.htaccess` fallback rules. Compatibility entry
points also support bookmarked pages such as `/network/` and `/leaderboard/`.
After enabling `FallbackResource /index.php` for the vhost, you may set
`ZOD_CLEAN_URLS` to `'1'` for short URLs throughout. Dedicated-server clients
can use `https://zodee.retrocs.org/index.php` as their hub URL without rewrites.

## Community features and database upgrade

Run `php _app/bin/migrate.php` from the installed document root after updating.
Migration 003 adds expiring browser-approved native game connections. Apply all
migrations before using the updated native browser. Production verification was
not performed as part of this change.

Migration 002 adds opt-in friends/presence, invitations, blocking, profile privacy,
one-use recovery codes and versioned community creations. Existing accounts keep
public profiles, with friend requests and presence sharing disabled by default.

The Community page manages requests, blocks and fifteen-minute server invitations.
Presence means activity on the portal within three minutes, not in-game status.
The account page provides session revocation and eight offline recovery codes.
Codes are shown once and stored hashed; each works once, and generating a new set
invalidates the previous set. Recovery revokes website sessions and unused join
tickets. It does not send email or automatically close existing game connections.

The Creations page accepts authored `.map` files and campaign `.zip` packs with a
PNG/JPEG screenshot. Versions include minimum engine requirements, file previews,
SHA-256 and author/version history. Authors can preview pending submissions;
public browsing and downloads require operator approval. Original game media must
not be uploaded. Downloads are explicit; the game still validates and previews
campaign packs before installation. Use editor Open for downloaded map files.

Set PHP `upload_max_filesize=8M`, `post_max_size=12M`, and MariaDB
`max_allowed_packet` to at least 16 MiB for the upload limits. Campaign ZIP support
needs the PHP zip extension. Screenshots are limited to 2 MiB and 2048 × 2048.
Submissions are stored in MariaDB, so include these tables in database backups.
Authors have a default 128 MiB total quota (`ZOD_CREATION_QUOTA_MB`) and up to
ten pending submissions, including versions awaiting review.
The Docker web target includes ZipArchive and these PHP upload limits.

Operator commands (from the document root):

```sh
php _app/bin/community.php --list
php _app/bin/community.php --id REVISION_ID --export /private/review-directory
# Review files and redistribution permissions before approval:
php _app/bin/community.php --id REVISION_ID --approve
php _app/bin/community.php --id REVISION_ID --hide
php _app/bin/community.php --server SERVER_ID --region Europe --mode Skirmish
```

Regions and modes are operator-supplied labels, not IP geolocation. The portal
filters region, map, mode, player occupancy, lobby state and browser-local
favorites/recent ticket requests. Real TCP latency, ping filters, persistent
favorites and successfully joined servers are available in the native browser.


## Operator moderation

Migration 004 adds private reports and moderation audit records. Add
`'ZOD_ADMIN_USERS' => 'YourExistingUsername'` to the installed `config.inc.php`
(or set the environment variable) to allow that account into `/index.php/admin`.
Leaving it empty denies all operator access. Existing local configuration files
are preserved during refresh and need this setting added manually.

Review pending revision downloads and previews, approve/hide uploads, enable or
disable provisioned servers, and resolve reports in that page. Report links on
public servers and creations prefill the private report form. New server secret
keys are still provisioned through `_app/bin/server.php`, never displayed in the UI.


## Account-owned dedicated servers

Migration 005 adds permanent server ownership, delegated server administrators and
expiring host sign-in requests. Run the migration tool before upgrading public agents.
The dedicated host's `zod-agent --mode public --login ...` command prints a browser
URL. Approval under a cloud account makes that account the owner. New IDs register
automatically; existing IDs need their existing host key and cannot be taken from
another owner. No account password is stored on the dedicated host.

**My servers** (`/index.php/my-servers`) lets the owner add/remove existing cloud
accounts as administrators. The owner is always an administrator; delegates cannot
change membership or ownership. Server roles are separate from global website
operator permissions. Public agents check ownership before launching and fetch
current administrator roles for in-game enforcement. Private/LAN hosting stays offline.

### Server availability (schema 008)

Before serving this version, run `php _app/bin/migrate.php` from the installed document root. It creates the live availability table idempotently. Update the dedicated-server agent and native engine together to publish free player slots, spectator and bot counts, difficulty and speed. Older agents remain accepted; their missing details are shown as unknown rather than guessed. No live deployment is performed by the local tests.

## Planetary Conquest season map

The War Room multiplayer hub is at `/index.php/war-room`. Select
`/index.php/war-room/conquest` for the planet atlas, territory maps and battle
history, or `/index.php/war-room/public-servers` for the listing, leaderboards,
public battle history and statistics. With clean URLs enabled, omit
`/index.php`.
Set `ZOD_CONQUEST_SNAPSHOT` in the installed config to an absolute path to a
JSON file outside the document root, readable by PHP. Publish updates by
atomically replacing the file. The page shows the supplied timestamp and
computes control percentages from sector ownership. Each atlas card shows the
faction shares for that planet and opens a map with every sector and its owner.
Without a valid snapshot, the atlas opens clearly labeled preview maps with example ownership.

The website reads snapshots only. Planetary Conquest gameplay and authoritative
season storage are still proposed. An operator can supply a snapshot in this
format when season data becomes available:

```json
{
  "season": "Season name",
  "updated_at": "2026-11-27T12:00:00Z",
  "planets": [
    {
      "name": "Planet name",
      "biome": "jungle",
      "rows": [
        [{"name": "North Crater", "owner": "red", "map": "Crater Crossing"}, null],
        [{"name": "Central Ridge", "owner": "blue"}, {"name": "South Basin", "owner": "neutral"}]
      ]
    }
  ]
}
```

Each planet has one rectangular grid. Use `null` for gaps. Owners are `red`,
`blue`, or `neutral`. A snapshot can contain up to eight planets, with up to
12 rows and 12 columns per planet. Gaps do not count toward control percentages.
Optionally set a planet's `biome` to `desert`, `volcanic`, `arctic`, `jungle`, or
`city`, a sector's `map` to its battle map name, and a sector's `start` to
`red` or `blue` to mark a faction starting sector. A faction controls the
whole planet when it holds every sector.

For Battle History, add an optional top-level `battles` array with up to 200
current-season records. Each record has `finished_at` (UTC ISO 8601), `planet`,
`sector`, and `result` (`red`, `blue`, or `draw`). Publish the latest records
in the snapshot. History is displayed newest first.
