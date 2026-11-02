# S2 green-flash follow-up

## Native update: watching-game flash reproduced

**Current result (21:49): S2 reproduced on the watching guest.** The user additionally reproduced it manually
in both directions using main checkout `coop_test.py play` in Sleeping Giant Inn. Only the held prop changes
colour; the holder's view stays normal.

The isolated branch was rebased onto dev `edca669f`, including `a805ec4b` (steered holds) and the dialogue merge.
Native captures used the exact DLL copied from the main checkout after the user's manual reproduction:
SHA256 `327b300c9cf4119c95f5c85c82715d63226fd2bf958f7bc44b8286176b270c8b`, reported build
`v1.6.7-798-gc6553525-dirty`. That build label predates the merge commit; the artifact identity is recorded
explicitly rather than claiming a newly compiled edca669f build. No production C++ changed in this worktree.

### Concrete recorded trigger

`green-flash-observer-of-host` enters Sleeping Giant Inn, grabs placed tankard `600EE` (base `319E3`), lifts it
with real mouse input and moves it sideways. Crosshair targeting and the actual grabbed reference are checked.
The other player faces the held prop. Both views are recorded losslessly at requested 120 fps.

At the `held at eye height` checkpoint the host's tankard retains its metal texture, while the watching guest's
same tankard is flat dark green. Sample pixel RGB is `(23, 42, 36)`. Full native frames, enlarged comparison and
videos are retained in:

`build/coop-test/green-flash/20261101-214802-observer-host-600EE-1eed/`

- `host-held.png`: holder, normal texture.
- `client-held.png`: watching guest, flat green.
- `holder-vs-watcher.png`: labelled native crops, enlarged; originals retained.
- `host.mkv` / `client.mkv`: 2,607 / 2,694 decoded frames, with `events.json` and per-frame body traces.

The guest's body is motion type 4 (keyframed) during the affected checkpoint. The holder's remains dynamic.
No active reference-effect entries were reported at sampled checkpoints. This does not exclude material,
lighting or render-pass changes; it is not a diagnosis of the graphics fault.

### Reverse-direction controls

`green-flash-observer-of-guest` completed the same tankard grab and movement with the guest holding and host
watching. Its capture has a dynamic/steered host body; no matching green fill was found in the bounded object
region or selected native frames. A second run, `green-flash-observer-of-guest-fast`, used a farther grab and
fast real mouse reversals. The guest remained holding at every captured phase, but the host recording still
had no comparable flat-green candidate. These negative controls do **not** overturn the user's manual
reproduction in that direction.

Captures: `20261101-214836-observer-client-600EE-93be/` and
`20261101-220052-observer-client-600EE-fast-8ae2/`, under the same `green-flash/` directory.
Logs: `build/s2-observer-attempt3.log`, `build/s2-observer-fast.log` and matching evidence directories.
Both scenarios' input/cleanup checks passed; visual outcome is recorded separately. Final own pair stopped,
prefix processes verified empty and duo released at 22:01 before offline analysis.

### Correcting the colour filter

The earlier strict filter required green at least 80 and excess 35 over both other channels. It missed this
actual dark teal/green fill. Therefore its zero-candidate results cannot be used to reject S2, even for the
new recordings. The viewer frames, not that threshold, established the reproduction.

The analysis script now accepts explicit channel thresholds and an object region. On the confirmed native
watcher crop (`592 145 650 199`), `--red-excess 10 --blue-excess 3 --minimum 20` finds the dark fill. The paired
holder's tankard-body crop (`600 385 660 440`) is a control (2,032 matching pixels on the watcher, zero on the holder). Colour candidates still require visual confirmation;
no universal colour test or no-flash assertion is claimed.

### Playback code checked; cause still open

The affected guest path is `BodyService::Show` → `BodyBridge::Drive` → `PlaceBody` → `FollowBody`.
The watching host receives `NotifyBodyFrames::Held` and selects `BodyBridge::Steer`, which uses velocity
steering nearby and a simulated placement beyond `kSteerFar`. Source and traces agree on these routes.
`PlaceBody` and `SteerBody` both call the native velocity setters; their IDs/signatures match the local
CommonLibSSE-NG reference. This does not identify which graphics state causes the green fill.

The player-driven native reproduction and visual evidence are saved. The engine fact behind the graphics
change still needs to be identified before adding it to the fake engine and writing the failing GameTests flow.
Unit tests and the fix follow that step; no production fix has been made.

### Fixture corrections and verification

The first attempt booted directly into the inn, where the global scenario baseline rejected arriving NPCs and
weather. It stopped before a grab. The next selected a plate under another item; the crosshair hit that item,
so the grab never began. Both are retained as fixture failures, not S2 results. The final probe starts from the
normal QASmoke baseline, enters the inn inside the fixture, uses a standalone tankard, and verifies the crosshair
before pressing Activate. Observer aiming uses actual body/camera positions.

Python compilation, all 31 harness tests, scenario discovery, configurable-mask pipeline checks and whitespace
checks pass. Native reproduction/capture is the validation for these Python-only additions. No C++ build or
full `scripts/check.sh` rerun is claimed for this reproduction-only change.

## Automatic harness detection (23:20)

The metal-tankard fixture now samples a 96×96 region around the aimed held body at 60 fps, throughout its
steady hold and mouse sweeps. A dark-green fill over 128 pixels fails the scenario and saves the strongest
candidate, per-frame sample times/counts, both full videos and engine traces. It requires at least 60 sampled
frames. The colour rule is specific to this vanilla metal-tankard fixture, not a universal object classifier.

The first continuous-check run failed automatically on unmodified playback: **18 of 340 sampled frames**, maximum
1,862 matching pixels. Evidence: `build/coop-test/green-flash/20261101-232042-observer-host-600EE-58f6/`,
`appearance.json`, `client-tankard.png`, and `build/s2-pixel-red.log`. A single end-of-hold screenshot had missed
the transient fill; full-video analysis found it, which prompted continuous sampling.

Fresh diagnostic builds record shader flags, material/texture identities, UV offsets/scales and geometry
transforms per frame. Those remain intact during the reproduced fill; no effect-data pointer appears.
The native body switches between keyframed playback while moving and dynamic motion again at the host's
reported rest, even though the host player still holds the prop.

Two controlled diagnostic artifacts still failed the same visual assertion:

- Applying `Drive` through `MainLoopTasks`: 2 of 361 frames, max 1,858 pixels (`build/s2-phase-probe.log`).
- Omitting the extra `NiAVObject::Update` after the body-to-node write: 1 of 363 frames, max 547 pixels
  (`build/s2-node-probe.log`).

Neither is a fix, and the source was restored after building each diagnostic artifact. These separate short
runs do not establish an improvement in flash rate. Each run stopped and verified its own prefix processes,
then released the duo before offline analysis or compilation.

The diagnostic additions pass the full `scripts/check.sh` gate: ClientTests 176, HostAuthorityTests 198,
TPTests 32 and GameTests 271 cases, harness checks, UI fixtures, DLL and launcher builds. The native S2 check
remains red. No S2 GameTests regression has been added yet.

The next controlled probes are lighting-list refresh after a manually moved keyframed node (HIGGS's
`ShadowSceneNode_UpdateNodeList`, verified on AE as 106349), and an overlay-disabled control. The cause is still
open; no GameTests engine fact or production fix is claimed yet.

## Earlier preparation (before the manual watching-game clarification)

User confirms the symptom happened previously and clarifies that **only the held object turns green**.
This is still an open report. A failed reproduction does not establish that it never happened.

Worktree `.worktrees/s2-green-flash`, branch `repro/s2-green-flash`, based on `b3a43770`.
No production change or merge. The fresh native probes below are **prepared, not yet run**: the main agent
owns the shared duo for an S4 floating-clutter soak. No S2 game was launched and its game was untouched.
S2 queued while preparing; its waiter is removed when handing back this blocked task so an idle reservation
does not hold up other agents. Rejoin the queue when resuming native work.

## More specific original scene

Both `1455-e7e` and `1455-a9c` snapshots place the players in `13A5C`, `IvarsteadVilemyrInn`:

- Host: `(1630.73, -142.27, 0)`.
- Guest: `(1499.33, 131.62, 0)`.
- Bassianus Axius: `(1369, -43, 0)`; native reference `19E08`, base `136C1`.
- The host timeline shows `9EF53` moving/resting around `(1363, -29, 105–107)` near report time. That is
  horizontally close to Bassianus and around head height. This is the strongest prop candidate, not proof
  of which object the user saw flash.
- Skyrim.esm identifies `9EF53` and `9EF52` as **Kettle01**, base `12FE6`, mesh `Clutter\Kettle01.NIF`.
- Earlier guest grabs name `9EF52`, `9EF53`, and then `9E194`; the last is an **alto wine bottle**, base `3133B`.

ESM extraction: `build/read-s2-records.py`, `build/s2-records.json`. Relevant grab lines:
`build/s2-original-hands.log`. Copies of the original host snapshots are under `build/2026-10-31-1455-*-host.json`.
Source reports remain read-only in the user's SkyrimCoop report folder and the shared Fabien archive.

Both reports reference the same last manual save, from about five minutes before the flash report. It was
copied into this worktree only; its thumbnail shows the inn corridor and no held object. It is not a screenshot
of the reported flash. The archived plugin list includes USSEP, Alternate Start and SkyrimCoopStart; the normal
duo profile differs, so scene staging alone will not reproduce every detail of the original installation.

## Previous recordings checked

The earlier free-drag recordings use a ladle, are 30 fps, and span about 3.5 seconds each. Every frame in these
eight videos was decoded and checked for a strict green-dominant pixel mask:

- `20261101-132643-drag-host`: 107 host frames, 111 guest frames.
- `20261101-132653-drag-client`: 106 host, 109 guest.
- `20261101-132730-drag-host`: 105 host, 106 guest.
- `20261101-132810-drag-client`: 105 host, 106 guest.

Total: **855 frames; no strict green candidates**. This agrees with the earlier visual result for those clips.
It neither rules out a shorter flash between captured frames nor the different kettle/NPC interaction.
Analysis files are in `build/s2-video-analysis/`.

`scripts/analyze_green_flash.py` ranks colour candidates for manual review. It is not a regression assertion
that S2 is absent, and it is not an exhaustive detector for every green tint. The decode/analysis pipeline was
checked with a synthetic four-frame video containing one 16×16 green square: counts `[0, 0, 256, 0]`.

## Fresh native probes prepared

| Scenario | Actions and evidence |
| --- | --- |
| `green-flash-ivarstead-host` | Host grabs original kettle `9EF53`, lifts it with real mouse input, lowers/sweeps it against Bassianus, releases. |
| `green-flash-ivarstead-guest` | Same prop/scene, guest holds it against the NPC copy. |
| `green-flash-ivarstead-handoff` | Contact and release, then the other player grabs/moves/releases the same kettle. |
| `green-flash-ivarstead-wine` | Original alto wine `9E194` as a material/shape control, including handoff. |

Setup stages the native NPC and original placed prop in the reported inn. Tested motion, grabs and releases
use engine input dispatch. A saved fixture restores the world afterwards. Both displays `:99` / `:100` are
recorded at a requested **120 fps**, losslessly with `libx264rgb`; actual capture rate and frame coverage still
need native verification. Body traces, shader-effect observations, player/camera state and input phases are
saved alongside both videos. No effect shader is applied or forced by the probe.

Scenario PASS means its input sequence and cleanup completed. **It does not mean no green flash occurred**;
the recordings must be reviewed after stopping and releasing own games. The diagnostic scenarios have their
own `green-flash`/`visual` tags rather than making a visual absence claim in the normal physics checks.

Offline preparation checks pass: Python compilation, discovery of all four flows, 31 harness tests,
the synthetic single-frame analysis check, and whitespace checks. There is no native result or GameTests
reproduction yet; no speculative fake-engine change or production fix is justified.

## Deployment and coordination

Known baseline DLL copied into this worktree only:
`0186e04875edae42f95fe5d59e34eade031b02ca545f8dfbb30233728951f9e0` (`b3a43770`, before S1's uncommitted physics fixes).
The current main physics artifact was also snapshotted read-only for a later comparison:
`dbf442b85240a56c6ddf5310925537bb85ab7b87de463ff50f37176296ee37c9`.
Neither was deployed into the other agent's running game.

`build/run-s2-baseline.py` is ready to run after rejoining the shared queue, once DUO is free and S2 is first. It asserts ownership,
atomically deploys/hash-checks the known baseline, verifies audio stays off the physical device, runs the four
probes, and stops/verifies/releases its own pair in `finally`. Offline frame analysis follows release.
Any confirmed flash should first become a reproducible scenario; only then should the usual GameTests/unit/fix
flow begin.
