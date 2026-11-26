# Website changelog

Changes to the zod-enhanced public website, PHP API and deployment are recorded
here, separately from the [game changelog](../changelog.md). Entries describe
repository changes, not an announcement that the live website has been updated.

## Unreleased

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
