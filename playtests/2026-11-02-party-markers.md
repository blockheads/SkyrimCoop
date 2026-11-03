# Party markers acceptance

Fabien R2: find the other players on the compass and map. Implemented on `feature/party-markers`, from
`dev` base `a5ddff75`, in `.worktrees/party-markers`. This batch is not merged into dev or pushed.

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
Both 1.7.104 and the separate 1.6.1170 DLL/loader compile. The new marker flows were not run natively on 1.6.1170;
its optional compatibility run was removed from the queue while the other agent continued using the duo.
Targeted marker tests have four GameTests flows / 62 assertions
and two ClientTests cases / 15 assertions.

The three service flows fail when the real marker service is omitted, then pass when restored. They exercise a
three-player party, names/identity, movement, departure/rejoin, exclusion of self/NPCs and coexistence with waypoints.

Screenshot review exposed a real renderer bug: an unhovered native pin's text field contained the name, but its
parent `TextClip` had zero opacity. The strengthened native scenario failed on that state. `FakeMapMovie` now models
this native behavior; the real label renderer failed four GameTests assertions before making the container opaque,
then passed. Native screenshots confirm readable labels after the fix.

## Native checks

Steam 1.7.104 / SKSE 2.3.1 / Address Library v13: both games boot, join and show the other player.
The final combined batch passes all five cases in 62.6 seconds:

| Scenario | 1.7.104 |
|---|---|
| `party-world-map-markers` | PASS: names, world/local maps, movement while open, both games |
| `party-markers-in-interior` | PASS: Sleeping Giant Inn, exterior world pin and actual local positions, both games |
| `party-compass-markers` | PASS: actual native icons drawn on both games |
| `party-markers-after-rejoin` | PASS: departure clears, rejoin restores one icon |
| `map-marker-reaches-everyone` | PASS: both players place the shared destination, then remove it |

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

## Retained evidence and scope

Private worktree `build/marker-evidence/` retains `check-label-final.log`, `native-label-final.log`,
`native-label-red.log`, `game-label-red.log`, `game-label-green.log`, original omission red/green logs,
`native-travel-control.log`, runtime build logs and `native-1.7.104/` screenshots/native-movie values. Executables,
vendor archives, images and IDA databases are not committed.

The direct compass call at `51668+0x128` to Invoke `82256` was checked in both actual executables; installation
verifies its opcode and destination. Map update `53111` and the typed camera/world/location layouts were checked
against CommonLib and both disassemblies. Version-specific IDA notes, engine references and annotation datasets
were regenerated. Existing shared IDA databases and the other agent's source/games remain untouched.

Every native batch used the shared DUO queue with private prefixes, and stopped/verified its own processes before
release or offline work. This acceptance covers the native Steam UI; it does not establish compatibility with every
third-party HUD/map replacement or a 250-player rendering stress test. Unprojectable or unrelated-space pins are
hidden, and coincident compass positions have no defined bearing.
