# S2 green-flash follow-up

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
