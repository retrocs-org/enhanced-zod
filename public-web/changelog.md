# Website changelog

Changes to the zod-enhanced public website, PHP API and deployment are recorded
here, separately from the [game changelog](../changelog.md). Entries describe
repository changes, not an announcement that the live website has been updated.

## Unreleased

### Added

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
