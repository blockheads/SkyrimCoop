# First-time joining through the bundled character start

Worktree: `.worktrees/first-time-join`, branch `first-time-join`, based on combined dev `42273179`.
No merge into dev is authorized. Preserve the other agent's later placement/record/harness fixes on dev when
integrating this work; do not replace those files wholesale.

The revised plan is in [join-flow.md](../design/join-flow.md#first-time-join-bundled-start-2026-10-29-v1).
An unknown friend starts joining, customizes and names a character in the bundled room, then automatically
continues to the original host. Ordinary New Game behavior stays unchanged. External Quick Start and its
message interception are no longer needed.

Implemented:

- Compatibility-gated creation, original endpoint/Steam identity/lobby retained, duplicate and automatic-host guards.
- Native main-menu cell startup and native race/naming menu; explicit name confirmation and setup completion.
- Clean prisoner starting kit, race abilities and vanilla race-dependent learned spells, then the existing full
  character/knowledge transfer into the host's story/world state.
- Customization without a deadline; startup/finalization reuse the loading timeout. Cancellation/host loss preserve
  creation; failed final joining retains the completed character for retry.
- Refusal UI waits until creation finishes, so losing the host does not steal the native creator's input.
- Bundled ESL-flagged [SkyrimCoopStart.esp](../../GameFiles/Skyrim/SkyrimCoopStart.esp), reproducible with
  [build_start_room.py](../../scripts/build_start_room.py). One new cell and 143 static/light references, with
  face-readable ambient light and a native COC marker; no quests, scripts, actors or interactive doors.
- Production [Steam launcher](../../Code/launcher/Main.cpp) preserves Steam's actual original arguments through
  SKSE in the child's environment. The real closed-game test showed that standard SKSE drops OS arguments and
  Skyrim's Steam Apps API returns no `joinlobby` parameters. The optional API remains a fallback for other URL
  launch configurations. See the [native references](../re/systems/character-start.md).
- No network-message or character-save format changes.

Validation:

| Check | Result |
| --- | --- |
| `scripts/check.sh` | PASS: client, authority, encoding, game integration, harness comparisons, UI, plugin and production launcher |
| Client / authority / encoding / game cases | 126 / 187 / 29 / 219; game suite has 2328 assertions |
| Native unknown friend: join → create/name → automatic arrival → save → kept-character rejoin | PASS |
| Native Breton learned spell and appearance/kit transfer | PASS, including Conjure Familiar on the host copy |
| Loaded-game joining; returning-character main-menu joining | PASS, both |
| Existing-character appearance changes; plugin compatibility refusal; unrelated story message box | PASS, all three |
| Real two-account in-game Steam invite → creation → join → save → re-invite as kept character | PASS |
| Real two-account Steam launching the closed guest for its accepted invite | PASS through the compiled production launcher; identity, appearance, spells, kit and host copy verified |
| Real Steam host loss during creation, then continue customizing and naming | PASS after the refusal-input fix; no automatic join/hosting |

The seven scripted native regressions and three real Steam/host-loss checks are distinct evidence from fake Steam
integration. The first closed-game attempt exposed the argument-loss issue; the launcher then passed the actual
Steam `joinlobby` route without a synthetic lobby or direct-transport join request. The second account's first-run
EULA was accepted with the user's explicit authorization; Steam installed its own compatibility runtime first.

Local verification logs are in this worktree's `build/`: `check-final-after-native.log`, `native-creation.log`,
`native-regressions.log`, `native-steam-invite.log`, `native-steam-launcher.log`, and
`native-creator-host-loss-fixed.log`. `creation-preview.png` shows the native creator. The initial UI failure is
retained in `native-creator-host-loss.log` and its corrected native rerun is recorded separately.

Testing used isolated prefixes whose CHAT.md/DUO.md link to the shared main checkout. Own games were stopped,
zero Skyrim processes verified, the temporary secondary-account Steam launch option restored, isolated client
prefix ownership restored to bighass, and test-deployed new room/launcher files removed from the shared install.
The duo was released and the next waiting agent notified. `scripts/net_check.sh` was not run.

Installation requires enabling the bundled room in both games and routing Steam's launcher through the bundled
production launcher for closed-game invites; [README](../../README.md#getting-started) describes manual setup.
