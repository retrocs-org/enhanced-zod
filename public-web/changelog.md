## Accurate server availability (unreleased)

- Show open player slots, spectators, active bots, difficulty and speed from authenticated host heartbeats.
- Filter open servers using actual free slots, including teams occupied by bots; preserve unknown fields for legacy agents.
- Validate and atomically replace live advertisements. Requires MariaDB schema 008: run `php _app/bin/migrate.php` from the installed document root before serving the updated site.
- Update the native engine/client and dedicated-server agent together. Existing original-game media and installed `config.inc.php` remain local.

## Integrated dedicated-server dashboard (unreleased)

- Bring ownership, delegated administrators/moderators, health, update status, player moderation, presets and audit history into the server console.
- Keep existing owner-only role changes and CSRF validation; provide clear role labels and dashboard section links.
- Improve focus on text areas and summaries, skip-to-content targeting, long build-ID wrapping and reduced-motion navigation.
- Uses existing schema 007; no new database migration. Install matching native client/server versions for the new build compatibility and rejoin handshake.

## Reliability tools and sharing (unreleased)

- Preview, upload/import and download portable named server presets, matching installed maps by SHA-256 and enforcing role/engine compatibility.
- Show running, staged and rollback builds with the last managed update outcome and timestamp in the server console.
- Surface authoritative-state check/mismatch counters and the host build identity with health freshness.
- Add scoped native moderation delivery-status polling; do not label uncertain outcomes as successful delivery.
- Update website/agent/engine together. These additions use the existing schema 007; no new migration is required.

## Match history and maintenance (unreleased)

- Add detailed, paginated public match history with teams, participants and privacy-aware identities; link profiles, server consoles and statistics to results.
- Add scoped native moderation and scheduled-maintenance APIs with timestamp-bound acknowledgements.
- Support administrator/owner maintenance scheduling and cancellation from the console.
- Improve keyboard focus, reduced-motion and forced-color support.
- Requires migration 007 plus matching agent/engine updates. Existing report retries remain compatible; legacy teams are shown as unknown.

## Public server operations (unreleased)

- Add server consoles with scoped moderator/admin/owner permissions, temporary bans and kick delivery status.
- Let owners disable hosting or revoke credentials; show recent role, authorization and operations audit history.
- Add named match presets, installed-map rotation, tactical previews and one-vote-per-participant between-round voting.
- Report heartbeat/health freshness, cloud connectivity, simulation timing and statistics queue size.
- Requires MariaDB migration 006 and matching engine/agent updates. Production deployment remains separate.

# Website changelog

Changes to the zod-enhanced public website, PHP API and deployment are recorded
here, separately from the [game changelog](../changelog.md). Entries describe
repository changes, not an announcement that the live website has been updated.

## Unreleased

### Server ownership

- Migration 005 adds account-owned servers, delegated server roles and expiring browser authorization for dedicated hosts.
- Owners manage server administrators under My servers; ownership is permanent and delegates cannot modify membership.
- Keep server administrator authority separate from global site operators. Record the affected account in role-change audit entries.
- Require ownership for public server API credentials and hide unclaimed legacy listings until the host signs in.

### Moderation and accessibility

- Migration 004 adds private community reports and an operator audit trail.
- Allowlisted existing accounts can review unpublished uploads, approve/hide revisions, enable/disable provisioned servers and resolve reports at `/admin`.
- Add report links on server and creation pages; operator access defaults to disabled until `ZOD_ADMIN_USERS` is configured.
- Show the engine version and map delivery information in server listings.
- Improve keyboard focus, narrow/zoomed layouts, wrapped text and reduced-motion support.

### Native connection approval

- Migration 003 adds expiring device requests with explicit signed-in browser approval and single-use ticket delivery to the native game.
- Password changes/recovery revoke approved pending requests; cleanup removes expired requests.

### Added

- Opt-in friends, recent portal presence, server invitations and blocking.
- Profile visibility, active-session management and one-use offline recovery codes.
- Moderated map/campaign sharing with screenshots, package previews, engine
  requirements, SHA-256 downloads and version history.
- Region/mode/occupancy/state filters and browser-local server favorites/recent
  join requests; operator tools label servers and review creations.
- MariaDB migration 002 and PHP ZipArchive support in the Docker website image.

- Self-contained PHP website and network API in `public-web`, backed by MariaDB.
- Public server directory, player profiles, leaderboards and unranked match
  statistics, with account authentication and public-server join tickets.
- A dedicated Features page with a responsive project timeline linking to the
  original Z and upstream projects, followed by multiplayer, networking portal,
  map/campaign editor and modern desktop platform highlights.
- Original Z and Zod Engine credits, public GitHub links and a field guide.
- Shared database and service configuration in `config.inc.php`, with environment
  overrides and support for an external database-password file.

### Changed

- Refined the website into a graphite and metal interface inspired by Z, with
  restrained panels and responsive layouts.
- Removed the Command Center section and moved feature details to their own page.
- Hid the API browser from public navigation while retaining its direct URL.
- Kept all website runtime code, administration tools and database migrations
  inside `public-web`; the separate hosting agent now lives in `server-agent`.
- Moved network integration tests to `tests/network` and hosting documentation to
  `docs/NETWORK-SERVICES.md`. Website release ZIPs exclude agent source.

### Fixed

- Refreshes deploy `public-web` directly into the vhost without generated wrappers
  or dependencies on a sibling application directory.
- Refreshes preserve the installed `config.inc.php`, lint PHP before deployment,
  set `wwwrun:wwwrun` ownership and restore the previous site if activation fails.
- Default `/index.php/...` routes and compatibility entry points support hosts
  without clean-URL rewrites; configured hosts can enable short URLs.
- Internal PHP code and configuration reject direct HTTP execution, with Apache
  rules protecting the internal application directory.
