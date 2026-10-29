# 2026-10-29: lock-history attribution and the join's button pose

Continuation of [the VM re-entry investigation](2026-10-29-save-vm-reentry.md), in
`.worktrees/v1-engine-fixes`, based on `ca906283`. Nothing is merged into `dev`.

## Lock warning: the reported writer was a reader

The third reload cycle reported an inversion between `1823d0 > 15ff00` and
`16f48b > 182f95` (Skyrim 1.6.1170 executable RVAs). The report called the second
pair write/write, but its saved stack passed through `HookLockForRead`.

IDA confirms all four reported lock acquisitions are reads:

- 12721, `ExtraAliasInstanceArray`: read-locks `this+0x28` while finding an alias package.
- 11737: read-locks the reference's extra-data list while checking its package list.
- 12094: the extra-data serializer, read-locks `this+0x18`.
- 12729: saves the alias array under its read lock at `this+0x28`.

The checker previously stored one set of callers/thread per lock-address pair, then ORed
together every mode observed for it. If later work wrote those locks, a conflict could be
reported with the earlier reader's callers and stack. The warning therefore did not establish
that the native save serialization on that stack took write locks. Neither does it establish
that no other path has a conflicting order.

The history key now includes both modes. Each mode has its own callers/thread, published only
after initialization. The table retains its existing capacity and entry size. No native lock
implementation or warning allowlist changes. The fast regression fails with the former key
because reader and writer evidence collapse to one entry; with the fix it passes 11 assertions.

Evidence: `build/lock-history-red.log`, `build/open-items-check.log`, and
`build/re/reload/decompile_open.json` (six native functions). The gate passes **543 cases /
5,783 assertions**, comparisons, UI fixtures and the Windows DLL. A fresh Engine Fixes run
then passes **30/30**: six rounds of the save regression, host and guest Beast Form/reversion,
host load and quickload. No strict lock audit fails, and no crash or freeze occurs. Both games
finish with zero frozen/saving/loading updates and zero update overlaps. Evidence:
`reload-freeze-open-items-stress-scenarios.log` and its per-game `*-trace.json` files.
This run includes the corrected history and precedes the full animation snapshot change below.

## Join mismatch: seven Dwemer button covers

The existing main-menu join scenario now reports individual references and nodes on failure.
It reproduces without additional fix mods: the kept character restores, but seven activators'
`CoverPlate01`, `CoverPlate01:8` and `CoverPlate01:18` nodes remain turned differently.

References: `0402A8AD`, `0402A1B8`, `0402A8AC`, `0402A89B`, `0402A89C`, `0402A8AA`,
`0402A8AB`. Their base is `0001BA5A`, `DweButton01`, model
`Dungeons/Dwemer/Animated/DweButton/DweButton01.nif` (read from the owned Skyrim.esm).
The model file has an HKX behavior graph and NiTransformControllers on the button and its cover;
the cover controller is active, clamps rather than loops, and spans 0 to 1/3 second.
This is not evidence to exempt the pose as an unsynchronized looping effect.

The behavior graph has Closed/Open/Ready/Press/Close states. The harness's
existing `graph_state` / `load_graph_state` commands now support non-actor references too;
actors retain their existing actor-state prefix. This uses the existing native serializers,
with no new production methods or raw-offset reads.

The probe finds identical serialized graphs in both games, in state `Ready`. Loading that same
host snapshot into the guest again does not change the wrong visible pose. Evidence:
`join-pose-probe-world.json`, `join-pose-probe-graphs.json` and `join-pose-probe-run.log`.

IDA also clarified a snapshot option. `TESObjectREFR::SaveAnimation` (20098) passes
`BGSSaveLoadGame::globalFlags & 4` to the graph serializer: true during a normal save, false
when unloading. That reaches `hkbCharacter` (65148), where false omits active clip times
(and another generator's state). Our snapshot always passed false; its comment incorrectly
said a normal save did so. A full-save experiment still fails on the same seven covers
(`full-graph-join-green.log`, despite the planned run's name). That candidate was reverted;
the existing snapshot mode and stream format remain unchanged. The misleading comment is corrected.
IDA output: `decompile_graph_save.json`, `decompile_character_save.json` and
`graph-full-save-evidence.txt`, under ignored `build/re/reload/` (the last file under `build/`).

Evidence: `build/join-pose-red.log` and `join-pose-red-{host,client}-trace.json`; ignored
asset inspection in `build/re/reload/DweButton01-blocks.json` and `behavior00.hkx`.
No asset is added to the repository. The controller format was checked against the
[NifTools specification](https://github.com/niftools/nifxml/blob/develop/nif.xml).

## Fix: publish restored local transforms

The next capture distinguishes each node's local and world transforms. **The cover's local
rotation is identical in both games; its world rotation is stale only on the guest.**
`node-pose-probe-world.json` records both matrices. The live nodes no longer carry the model
file's NiTransformControllers; that temporary inspector was removed. Local/world matrices
remain in the harness's detailed `world_state` response.

`ReferenceStates::ApplyGraph` now updates the reference's scene graph after restoring its
graph state: `NiAVObject::Update` (70251), with typed `NiUpdateData::kDirty`. IDA confirms
the call dispatches the downward update and updates the parent's bounds; its signature and
context layout match CommonLibSSE. The context's size and field offsets are asserted.

`GuestEngine` also applies cell graph rows when the serialized bytes already match. Equal
graph state does not establish that the engine has propagated its local transforms to the
visible world transforms. Other matching rows retain their existing shortcut. This affects
reference graphs on cell restoration, not actor animation replay or its wire format.

This requires Skyrim's actual scene graph: the existing native join scenario fails before
the change and passes its pose assertion afterward. A fake serializer returning the same
bytes cannot expose an unpublished native world matrix. The scenario is now tagged `v1`.

The first corrected standalone join passes the cover assertion, then fails its separate
postcheck because arrival displaced the host about 50 units (`graph-pose-green.log`).
`--fail-fast` skips its two planned repetitions. Preserve this as a failed scenario, not
three passes. It is separate from the corrected pose and was not hidden by changing tolerances.

## Final validation

The final source passes **543 cases / 5,783 assertions**, harness/ragdoll comparisons,
UI fixtures and the Windows DLL (`graph-pose-final-check.log`). The final DLL/PDB is
archived in ignored `build/graph-pose-final-binary/`.

With all six pinned fix mods active, the focused follow-up completes **14/15, no crashes**.
The full main-menu join scenario passes, as do save, host load/quickload, both transformations,
puzzle-ring restoration, both players' furniture animations, ability retention, effects on a
later copy, guest story events, crafted-potion retention across rejoin and checkpoint recovery.
Vampirism retention/rejoin completes its behavior checks, then fails the world oracle on
three clutter references (`32AFF`, `32AFE`, `32B00`, base `319E5`): only movement-record flags
differ. This is retained as a failed check. The earlier host-arrival displacement is retained too.

Both final games report zero frozen/saving/loading world updates and zero overlaps. The
checker remains enabled, with the existing known-pattern list unchanged. This follow-up is
not a rerun of the entire earlier 27-case profile and does not clear its other death/marker
failures. Evidence: `graph-pose-combined-ussep-matrix-scenarios.log`, the matching profile
manifest and per-game `*-trace.json` files. See [the mod report](../../mod-compatibility.md).

All native runs use the shared duo reservation and :99/:100. The handoff with the dialogue
agent was coordinated directly through its Claude terminal and DUO.md. Own games are stopped,
the reservation and own waiting entry released, and temporary mod/profile changes restored.

The historical intermittent werewolf crash remains open. The online follow-up includes
[upstream #863](https://github.com/tiltedphoques/TiltedEvolution/issues/863) and its
[descriptor fix #917](https://github.com/tiltedphoques/TiltedEvolution/pull/917), already covered
by our existing project/type checks; this is not another independently reproduced cause.
This pass completes 14 host/guest transformations and reversions without a crash (12 in the
stress run, two in the final combined profile). That supports the existing fixes; it does not
independently identify the cause of every historical intermittent crash. Longer gameplay and
acceptance on the eventual merged build remain necessary. No extra crash workaround is added
without evidence.
