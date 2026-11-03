# Party markers acceptance

Fabien R2: find the other players on the compass and map. Implemented on `feature/party-markers` (`e4f7bd71`), from
`dev` base `a5ddff75`, in `.worktrees/party-markers`. Reviewed and merged into dev at the user's request, including
the newer checked-actions/exclusive-holds commits. No push was requested or performed.

Other players have cyan destination-style compass icons and cyan pins with their character names on the native
world and local maps. Pins follow the displayed copies while the map stays open. Same-interior players appear at
their building's exterior marker on the world map and their actual positions on its local map. Leaving/disconnecting
clears the corresponding icons; rejoining restores one per player. The shared destination waypoint remains independent.

[Design and engine references](../docs/design/party-markers.md). The feature adds no network messages, quest targets,
saved markers or replacement SWFs. It uses native marker assets, visibility and camera projection.

## Fast checks

`scripts/check.sh` passed after the final code change:

| Suite | Cases | Assertions |
|---|---:|---:|
| ClientTests | 190 | 2,897 |
| HostAuthorityTests | 198 | 2,173 |
| TPTests | 32 | 335 |
| GameTests | 281 | 2,776 |

Harness lifecycle, runtime selection, comparison checks, UI fixtures and Windows DLL/launcher builds also passed.
Both 1.7.104 and the separate 1.6.1170 DLL/loader compile. The later review run also validates the marker flows
natively on both versions, as recorded below.
Targeted marker tests have four GameTests flows / 62 assertions
and two ClientTests cases / 15 assertions.

The three service flows fail when the real marker service is omitted, then pass when restored. They exercise a
three-player party, names/identity, movement, departure/rejoin, local-player exclusion and coexistence with waypoints.

Screenshot review exposed a real renderer bug: an unhovered native pin's text field contained the name, but its
parent `TextClip` had zero opacity. The strengthened native scenario failed on that state. `FakeMapMovie` now models
this native behavior; the real label renderer failed four GameTests assertions before making the container opaque,
then passed. Native screenshots confirm readable labels after the fix.

## Native checks

Steam 1.7.104 / SKSE 2.3.1 / Address Library v13: both games boot, join and show the other player.
The original feature batch passes all five cases in 62.6 seconds. The reviewed integration repeats them on both
runtimes: 59.0 seconds on 1.7.104 and 58.7 seconds on 1.6.1170.

| Scenario | Check on both games | 1.7.104 | 1.6.1170 |
|---|---|---|---|
| `party-world-map-markers` | Names, world/local maps, movement while open | PASS | PASS |
| `party-markers-in-interior` | Sleeping Giant Inn, exterior world pin and actual local positions | PASS | PASS |
| `party-compass-markers` | Actual native icons drawn | PASS | PASS |
| `party-markers-after-rejoin` | Departure clears, rejoin restores one icon | PASS | PASS |
| `map-marker-reaches-everyone` | Both players place the shared destination, then remove it | PASS | PASS |

The old `a5ddff75` DLL fails the original two party-marker scenarios, as expected, and passes the existing shared
waypoint control. An earlier expanded feature batch timed out at the guest's `LoadWaitSpinner` after travel,
before opening any map. The failure evidence is retained; the subsequent complete feature batch above passes.
No timeout, comparison threshold or loading check was weakened.

The primary-runtime screenshots below are genuine captures from that final batch. “Prisoner” is the test character's
name. The compass icon is above the other character; map pins have a name beside them.

These image links resolve inside the feature worktree, where the private evidence is retained:

![Party compass icon](../build/marker-evidence/native-1.7.104/compass-host.png)
![Named world-map party pin](../build/marker-evidence/native-1.7.104/world-map-host.png)
![Named interior local-map party pin](../build/marker-evidence/native-1.7.104/local-map-interior-client.png)

## Code review and integration

No blocking correctness findings. The review checked roster filtering and identity, update/disconnect/teardown
ordering, finite positions and coordinate-space separation, the mutex snapshot handoff, managed Scaleform value
ownership, native label opacity, map group recreation/hover depth, waypoint independence, and hook signatures and
patch restoration. No further production-code changes were needed after the feature acceptance.

The integration with `826eec44` includes the newer checked actions and exclusive holds. Its `scripts/check.sh`
passes in 109 seconds: ClientTests **193 / 3,757 assertions**, HostAuthorityTests **198 / 2,176**, TPTests
**32 / 335**, GameTests **282 / 2,783**, plus the harness/UI/runtime checks and Windows plugin/launcher builds.
Both runtime builds and all ten native checks above pass on that combined code. Subsequent documentation-only
commits from dev are preserved in the merge. The final source tree is checked against the code that was tested.

## Retained evidence and scope

Private worktree `build/marker-evidence/` retains `check-label-final.log`, `native-label-final.log`,
`native-label-red.log`, `game-label-red.log`, `game-label-green.log`, original omission red/green logs,
`native-travel-control.log`, runtime build logs and `native-1.7.104/` screenshots/native-movie values. Executables,
vendor archives, images and IDA databases are not committed.

Review evidence adds `review-check.log`, `review-legacy-build.log`, `native-review-primary.log`,
`native-review-legacy.log`, and `native-review-1.7.104/` / `native-review-1.6.1170/` screenshots and movie values.

The direct compass call at `51668+0x128` to Invoke `82256` was checked in both actual executables; installation
verifies its opcode and destination. Map update `53111` and the typed camera/world/location layouts were checked
against CommonLib and both disassemblies. Version-specific IDA notes, engine references and annotation datasets
were regenerated. Existing shared IDA databases and the other agent's source/games remain untouched.

Every native batch used the shared DUO queue with private prefixes, and stopped/verified its own processes before
release or offline work. This acceptance covers the native Steam UI; it does not establish compatibility with every
third-party HUD/map replacement or a 250-player rendering stress test. Unprojectable or unrelated-space pins are
hidden, and coincident compass positions have no defined bearing.
