# Planetary Conquest — proposed specification

Implementation remains paused. These rules are a proposal for review, not a released mode.

Start with an operator-enabled, invite-only seven-day test season on one planet
with twelve sectors. Red and Blue each start in an opposite corner, with the rest
of the sectors initially Neutral. Arrange sectors so each starting corner has
multiple routes across the planet. A faction can launch an attack only from a
sector it owns and can target only an enemy or Neutral sector that shares a
border. Check ownership and adjacency at reservation and resolution.

Each sector has three control points. An attacker victory removes one, a defender
victory restores one up to three, and a draw leaves control unchanged. At zero
points, ownership transfers and control resets. Capturing the opposing capital
ends the season. At the deadline, the faction with the most sectors wins, with
ties a draw. Store UTC, display local times, and archive each season before an
explicit reset.

Cloud accounts choose a faction fixed after participation. Permit one active
battle per account and sector and two attacks per faction. A Neutral sector uses
a server-controlled AI defender. A faction-owned sector requires one human on
each side, with no bots or self-matches. Never infer account identity from shared
IP addresses. Publish attack windows and cancellation rules before joining.

## Front lines and matchmaking

[World Domination Tour](https://cnc.fandom.com/wiki/World_Domination_Tour)
provides the reference for a shared territory map, faction battles, and
campaign resets. Planetary Conquest uses original ZOD planets and sectors.

- **Sector battle setup.** Give each sector an authored battle map and a fixed
  ruleset for the season. Show the map, defender, and remaining control points
  before a player queues.
- **Contested sector queue.** An attacker selects a reachable sector and queues
  there. For a faction-owned target, match an opposing player queued to defend
  that sector. For a Neutral target, reserve a match with the AI defender.
- **Moving front lines.** Apply the test season's control-point rules after each
  confirmed battle. Recompute attackable sectors on both sides when ownership
  changes. Show the available routes from friendly territory and the outcome
  that opened or closed each route.
- **Season reset.** Archive final standings, ownership, and battle history.
  Start the next season with one HQ per faction on each active planet and every
  other sector Neutral. Show the next season's start time on the atlas.

After testing the per-battle rules, consider scheduled resolution as a separate
season ruleset. Tally verified results for each sector within a published
window, then apply control changes at the cutoff. Settle ties without a change
of control. Display the cutoff and pending results so players can distinguish
them from confirmed ownership. Larger planets and multi-planet seasons can
follow once the first planet has enough players to sustain its queues.

## Hosting and result validation

Only healthy, approved, opted-in dedicated hosts receive reservations. Pin build, protocol, map digest and rules; do not mix normal matches with an active conquest reservation. A reservation expires if its required human players are not ready within five minutes. Recheck authorization at start. Bind its opaque grant to server, roster, season, sector revision and content.

The PHP API and MariaDB are authoritative. Accept results only from the assigned host with the correct grant and roster. Resolve once in a transaction using fixed lock ordering and a unique idempotency key. Identical retries return the same receipt; conflicting duplicates fail. Stale adjacency or revision archives the battle without ownership changes. Servers keep a durable result outbox until acknowledgment. Clients cannot declare victory.

Allow ninety seconds to reconnect. A confirmed departure after start may forfeit. Pre-start departures, simultaneous disconnects, host crashes and missing results cancel without territory changes. End unresolved battles after forty-five minutes. API outages allow existing battles to finish and retry delivery until a published deadline, but prevent new reservations.

Proposed tables: seasons, sectors, memberships, host capabilities, reservations, participants, results and append-only audit events. Proposed operations: map/history reads, faction choice, attack reservation/cancellation, host ready/start/result, and scoped operator controls. Apply browser CSRF protection, authorization, rate limits, bounded pagination and transaction-conflict handling. Migrations must never enable a production season automatically.

The native client and website show authoritative revision, freshness, ownership text alongside colors, eligibility reasons and pinned battle rules. Expired assignments cannot join. Operators can pause a season, quarantine hosts and make reasoned audit-recorded corrections. A server owner is not a global conquest administrator.

Before public launch, test Neutral attacks with an AI defender, opposing factions changing territory exactly once, forged, wrong-host and expired submissions; duplicate conflicts; competing reservations and lock ordering; reconnect/forfeit/crash/outage deadlines; backup restoration and season archiving. Normal public, private and LAN play must remain independent and work with conquest disabled.
