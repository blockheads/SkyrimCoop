# Reverse engineering notes

Everything we know about SkyrimSE.exe **1.6.1170** (Steam AE build) and how SkyrimCoop
touches it. The repo is the record; IDA is a workbench that syncs into it.

| Where | What | Edited by |
|---|---|---|
| [ENGINE_REFERENCE.md](ENGINE_REFERENCE.md) | Every engine address our client uses: community name, CommonLib/SKSE source, hook vs call, file:line, flags | generated |
| [systems/](systems/) | Notes per engine system: how it works, what we hook, gotchas | hand-written |
| [crashes/](crashes/) | One page per crash investigation: dump, call chain, what was ruled out, fix | hand-written |
| `ida_notes.tsv` | Names/comments exported from IDA (`ida_export.py`) | generated from IDA |

## Sources (in priority order)

1. **Our IDA database** (`build/re/SkyrimSE_1.6.1170.i64`, not committed) via `ida_notes.tsv`.
2. **CommonLibSSE** (powerof3, GPL-3) in `reference/CommonLibSSE`: engine classes, layouts,
   vtables, RTTI, and `RELOCATION_ID(SE, AE)` for ~9.7k addresses. The best reference for
   struct layouts; check our hand-written structs in `Code/client/Games/` against it.
   **CommonLibSSE-NG** (alandtse) in `reference/CommonLibSSE-NG` fills the gaps: ~170 more AE IDs,
   AE layout fixes (e.g. `PlayerControls::data` at 0x24, the `ButtonEvent` stride) and helpers such
   as `ButtonEvent::Create`. When the two disagree on an AE layout, check IDA.
3. **SKSE 2.2.6 source**, shipped with SKSE in the game dir (`src/skse64`): raw 1.6.1170
   offsets. Also shows which engine functions SKSE itself hooks (we stack on some of them).
4. **meh321 AddressLibraryDatabase** (`skyrimae.rename`): names for ~42k Address Library IDs.
5. **SKSE plugins** in `reference/plugins` (Precision, TrueHUD, EngineFixes, po3-Tweaks, Papyrus
   Extender): where other mods hook or call an address and how. Each such ID gets "Used by plugin:
   file:line" in its IDA comment, and their offset names fill in where meh321 has only `sub_`.
   Add a plugin to `fetch_references.sh` when it works in a system we touch.

Not every mod that does what we need uses Address Library IDs: some only call CommonLib APIs
(e.g. input injection, see [systems/input.md](systems/input.md)). When the engine path is
unclear, search GitHub for mods doing the same thing before reversing it.

Not available: Tilted Phoques' own IDA work. They ran a private shared database (they forked
IDArling); it was never published. ~76 of our addresses have no public name; IDA is the only
way to document those.

## Workflow

```bash
scripts/re/fetch_references.sh   # clone/update reference/ (CommonLibSSE, meh321 names)
scripts/re/build_re_data.py      # regenerate ENGINE_REFERENCE.md + build/re/annotations.json
scripts/re/make_ida_db.sh        # first time: unpack the exe, analyse, annotate (slow)
```

In IDA (open `build/re/SkyrimSE_1.6.1170.i64`):

- **File > Script file… > `scripts/re/ida_apply.py`** after `build_re_data.py` to refresh names
  and comments. It never overwrites names you set, and keeps comment text you write above the
  `[re]` marker line. Orange functions are hooked by SkyrimCoop, green ones are called/read.
- **File > Script file… > `scripts/re/ida_export.py`** to push your names/comments into
  `ida_notes.tsv`, then run `build_re_data.py` and commit both.

The exe in `SkyrimSE.exe` is encrypted by SteamStub DRM; IDA needs the copy unpacked by
Steamless (`make_ida_db.sh` does it). Never commit the exe or the database (Bethesda code).

## Crashes

Test games run with [CrashLoggerSSE](https://github.com/alandtse/CrashLoggerSSE) (pinned and
deployed by `scripts/coop_test.py`). Any harness wait fails at once when a game crashes, printing
the report; `scripts/coop_test.py crash` prints the newest crash of each game again. The report
(`scripts/re/crash_report.py`) has:

- the exception and CrashLogger's access-violation analysis;
- the objects the registers and stack point to (RTTI types, form IDs and names, scene node names);
- the probable call stack, engine frames named from `build/re/annotations.json`;
- our breadcrumbs: the last engine changes our code made through `SkyrimBridge`, with times
  before the crash and repeat counts (`Code/client/Breadcrumbs.h`, written by our crash handler);
- the last lines of `tp_client.log` before the crash.

A crash with no breadcrumbs in the last second or so did not start with something we just did.

**First: is it us?** Make the crash a recipe in `REPROS` (`scripts/coop_test.py`) and run
`coop_test.py repro NAME --vanilla`: the same commands, frame and camera with a stand-in plugin
(`Code/vanilla_harness`) instead of ours. A hand-typed vanilla run is not the same test.

**Crashes on freed memory** (garbage in a register, a fault far from the cause) are the common
kind in the engine: something deleted an object another system still lists. The heap tools
(`Code/client/HeapHistory.h`, off unless asked for) say who freed what:

1. `coop_test.py repro NAME --heap-history`: every engine allocation and free of the last seconds
   with its stack; the report gives each register's and stack value's block its story
   (allocated by, FREED by, reused by).
2. `--follow`: a second run watching the crash's register addresses from boot (they repeat when
   the game boots the same way), for objects allocated long before the crash.
3. `--guard SIZE` (the freed block's size from step 1): blocks of that size get pages of their
   own, inaccessible once freed. The game then faults at the first access after the free, in the
   code that still holds the pointer, and the report names who freed it. `--quarantine` (poison
   freed memory) is cheaper but misses stale pointers that are only read and compared.

4. `--probe ID,ID`: engine functions (Address Library IDs) whose calls go into the breadcrumbs,
   for the order of events: did the engine run X before the crash, how often, on what.

`docs/re/crashes/2026-10-11-qasmoke-exit-portal-graph.md` is the worked example.
For a hang, press Ctrl+Shift+F12 in the game for CrashLogger's thread dump; our `HangWatchdog`
logs the main thread's stack to `tp_client.log` after 10s.

## Conventions

- Address Library **IDs**, not raw offsets, in code: `POINTER_SKYRIMSE(type, name, ID)`.
  Look an ID up in `ENGINE_REFERENCE.md` before relying on our variable name; several are
  misleading (e.g. `cVMDestructor` is really `PlayerCharacter::dtor_impl`).
- A struct field offset we depend on gets a `static_assert` and, if it came from CommonLib or
  IDA, a comment saying so.
- New hook: add a note to the matching `systems/` page (why, what it replaces, SKSE overlap).
