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
3. **SKSE 2.2.6 source**, shipped with SKSE in the game dir (`src/skse64`): raw 1.6.1170
   offsets. Also shows which engine functions SKSE itself hooks (we stack on some of them).
4. **meh321 AddressLibraryDatabase** (`skyrimae.rename`): names for ~42k Address Library IDs.

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

## Conventions

- Address Library **IDs**, not raw offsets, in code: `POINTER_SKYRIMSE(type, name, ID)`.
  Look an ID up in `ENGINE_REFERENCE.md` before relying on our variable name; several are
  misleading (e.g. `cVMDestructor` is really `PlayerCharacter::dtor_impl`).
- A struct field offset we depend on gets a `static_assert` and, if it came from CommonLib or
  IDA, a comment saying so.
- New hook: add a note to the matching `systems/` page (why, what it replaces, SKSE overlap).
