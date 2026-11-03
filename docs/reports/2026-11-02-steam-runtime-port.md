# Steam Skyrim 1.7.104 port assessment

Assessed 2026-11-02 against `dev` commit `eb913f95`, in worktree `.worktrees/steam-runtime-audit`, branch
`audit/latest-steam-runtime`. This is an offline assessment, not a completed port or a gameplay acceptance run.

**The port looks practical: budget approximately 5–10 engineering days for a tested 1.7.104 build, including the
IDA/reference tooling.** A first successful launch may take 1–3 days; that is a much smaller milestone than a working
co-op game. Keeping both runtimes supported in one DLL could add 2–5 days. These are planning estimates, with medium
confidence: the exact 1.7.104 Address Library and native regression results remain outstanding.

Follow-up: upstream's completed compatibility patch is identified below. It supplies the core decoder and layout
changes, reducing implementation uncertainty. The 5–10 day figure is a conservative budget for our full port,
reference migration and acceptance, not an estimate for writing that small compatibility patch from scratch.

## Target and available artifacts

The latest published Steam runtime checked today is **1.7.104.0**, supported by published **SKSE 2.3.1** and
**Address Library v13**. The official SKSE download page and its Nexus Steam file agree. SKSE GitHub master already
contains a 2.3.2 changelog entry; that is not the published download version used for this assessment.

- [SKSE official downloads](https://skse.silverlock.org/)
- [SKSE published Steam files](https://www.nexusmods.com/skyrimspecialedition/mods/30379?tab=files)
- [Address Library v13 files and exact Steam runtime](https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files)
- [Bethesda August update and August 27 hotfix notes](https://elderscrolls.bethesda.net/en-US/news/skyrim-update-august-20)

Read-only public Steam app-info, retrieved through `api.steamcmd.net/v1/info/489830`, reports public build `24914197`.
The local Steam manifest records that same build, but the active `SkyrimSE.exe` is **1.6.1170.0**. The saved sibling
`SkyrimSE_1.7.104.exe` really is **1.7.104.0**, checked using its PE version resource; its SHA-256 is
`846efccf0c1374d71f892907f46549560f2fcb0a75cb87a3eed438baa0f1402f`.
Thus the manifest alone cannot select the correct runtime. Both files are under
`/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition/`.

I copied and unpacked the saved executable privately under this worktree's `build/runtime-audit/`, using the existing
Steamless tool through Mono. Neither game installation was changed. The live shared IDA instance `gy8k` remains on
`SkyrimSE_1.6.1170_unpacked.exe`; read-only metadata reports 194,094 functions and a type ordinal limit of 225.

The exact **1.7.104 Address Library binary was not available locally**. The current CommonLibSSE-NG repository supplies
a real **1.7.99** format-5 fixture, which I decoded for a preliminary migration inventory. Every coverage number below
is explicitly for **1.7.99**, not a claim that 1.7.104 is mapped or supported. The public meh321 `skyrimae.relib` still
ends at 1.6.1179 and cannot fill this gap.

## What Skyrim Together changed

Their [1.8.1 release](https://github.com/tiltedphoques/TiltedEvolution/releases/tag/v1.8.1), published September 14,
explicitly advertises 1.7.104 support and points to [PR #885](https://github.com/tiltedphoques/TiltedEvolution/pull/885).
I checked the actual patch, commit `6f9634979310cf1887a1dbf8f74ef13b71d0387a` (August 22): **11 files, 98 added lines,
67 removed lines**, comprising these compatibility changes:

| Upstream change | Relevance to our fork |
|---|---|
| Split the existing library reader into `LoadV2` and `LoadV5`; the latter reads a dense ID-indexed offset array and skips zeros | Directly reusable approach in our closely related `VersionDb.h`; add validation when adapting |
| Move the declared player fields by eight bytes and the VM pointer/frozen state by sixteen; use the typed VM in its update hook | Matches our binary findings; adapt to our expanded structs and additional player-field uses |
| Handle a newer SteamStub header with an unencrypted code section | Belongs to their custom immersive launcher; our SKSE plugin does not load the game executable this way |
| Move SKSE startup earlier, after Together installs hooks and before the game continues initialization | Belongs to their path that loads SKSE; our plugin is loaded by SKSE |

That patch retained the existing address IDs and did not introduce a replacement hook inventory. It does not contain
an IDA database migration. Upstream later added an explicit 1.7.104 version gate in
[`24dc4c4e`](https://github.com/tiltedphoques/TiltedEvolution/commit/24dc4c4e8c4744b36ff86f1f129a1c8e3d716356).

On October 21, [`e1de917c`](https://github.com/tiltedphoques/TiltedEvolution/commit/e1de917ce206d8df35982584e11272a6d569c1bc)
added `skyrim_version=latest|1.6.1170`: compile-time layout branches and separate CI builds. This is a useful model
for retaining our old build while adding the new runtime; it is not one DLL switching its ordinary field offsets at
runtime. Source/history was inspected through upstream `711ca1b832790e9035fd05c88a728e88f69c9b4b`.

Upstream's public `Tools/Scripts/addrlib.py` also decodes both formats. Running it on the public 1.7.99 fixture produced
the same 435,154 nonzero IDs and the same missing 52488 as our independent check. This is useful reusable reference
tooling, although it cannot supply the absent local 1.7.104 library.

The appropriate starting point is adapting those known decoder/layout changes, then handling our added engine
members, event sources, wait hook and versioned IDA tools. No upstream gameplay or launcher merge is needed to reuse
that work. The initial assessment should have consulted this upstream patch before estimating implementation effort.

## Size of our engine interface

| Inventory at the audited commit | Result |
|---|---:|
| Distinct IDs in our generated engine index | 606 |
| Indexed use sites | 698 |
| Function-entry hook IDs | 219 |
| RTTI registrations outside that index | 2,806 |
| Observed literal IDs including direct `VersionDbPtr` declarations and RTTI | 3,457 |
| Indexed IDs present in the 1.7.99 fixture | 605 / 606 |
| All observed IDs present in the 1.7.99 fixture | 3,453 / 3,457 |
| Saved IDA note IDs present in that fixture | 234 / 235 |

The generated index omits direct `VersionDbPtr` declarations, including several byte-patch bases, plus the RTTI
registrations. It must be widened before it can gate a runtime port. The literal scan also includes harness code;
computed IDs and conditional initialization still need review. The 3,457 number is an observed inventory, not a count
of functions that need manual reversing.

Three unresolved RTTI registrations (`392519`, `394235`, `398437`) are absent from **both** libraries; they are existing
gaps, not new 1.7 regressions. The one newly absent indexed ID is **52488, `SleepWaitMenu::OK`**, used by
[`WaitBridge.cpp`](../../Code/client/Bridges/WaitBridge.cpp).

I independently recovered the 1.7.104 OK callback from the executable's RTTI, primary vtable slot 1
(`SleepWaitMenu::Accept`), and the function registered with the literal callback name `OK`:

| Function | 1.6.1170 RVA | 1.7.104 RVA |
|---|---:|---:|
| SleepWaitMenu::Accept | `0x95DEF0` | `0x975190` |
| Registered OK callback | `0x95E000` | `0x9752A0` |

This establishes a recovery route without guessing by address proximity. Its correct 1.7.104 library ID and callback
behavior/signature must still be checked before changing the hook. Missing from the 1.7.99 library does not establish
that the old ID is absent in v13's 1.7.104 file.

## Concrete implementation work

| Area | Required work and evidence |
|---|---|
| Runtime selection and SKSE metadata | `skse_entry.cpp` always passes `1.6.1170.0` to initialization. Select the actual supported runtime, refuse unsupported versions before engine access, and update the client, reload loader and vanilla harness declarations. Review the `NoStructUse` claim: the client uses many fixed engine layouts. Add the SKSE `AddressLibraryV5` capability only once it is implemented. |
| Address Library decoder | `VersionDb.h` accepts only format 2. Add format 5 alongside it, validate the header/runtime/bounds, treat zero entries as absent, and report missing required IDs before installing hooks. The independent Python decoder in `build_re_data.py` also rejects the v5 fixture. |
| Player layout | Our `PlayerCharacter.h` fixes fields at 1.6.1170 offsets. The new `BSSystemEvent` sink shifts the later player layout by `+0x8`; update the typed layout/accessors and assertions for the selected runtime. This affects skills, perks, appearance, grabbing, shared rest, travel and combat flags. |
| Papyrus VM layout | Our `Misc/GameVM.h` reads the VM pointer at `+0x200` and frozen state at `+0x680`. Updated upstream bindings move these to `+0x210` and `+0x690`. The new Amiibo event bases change the layout by `+0x10`; the VM update loop and every use of `virtualMachine` depend on this. |
| Event-source subobjects | `PlayerCharacter.cpp::PositionPlayerSource` hardcodes `+0x388`; actual 1.7.104 RTTI says `+0x390`. A corrected singleton address alone does not correct this pointer. |
| Hooks and byte patches | Verify all 219 entry hooks for valid mapping/signature, then inspect every interior patch/call replacement. Known sites are in `UI.cpp`, `LiveMenuHooks.cpp`, `SkillsMenu.cpp`, `BSGraphicsRenderer.cpp`, `BGSStoryManager.cpp`, and `BSTaskletManager.cpp`. Record expected instruction bytes for each supported runtime. |
| Input and other changed classes | Upstream adds motion-gesture/sixaxis virtual slots to player/menu input handlers and fields to several menus, graphics state and VM structures. Audit what we actually call or instantiate. Our thin `PlayerInputHandler` only declares a destructor and enabled flag, so those slot changes alone do not prove that wrapper is broken. Avoid mechanically shifting unrelated vtables or every class mentioned in upstream's changes. |
| Harness, packaging and diagnosis | Select the runtime-specific SKSE DLL and library file in Linux/Windows staging, profiling and package pins. Verify the pinned CrashLogger build on the new runtime. Select crash annotations by executable version so old and new crashes cannot be symbolicated with each other's RVAs. |

Upstream implementation references, pinned during this audit:

- [SKSE PluginAPI metadata and AddressLibraryV5 flag](https://github.com/ianpatt/skse64/blob/25b72352adb6543fa6d0bd3795780672b2e238e0/skse64/PluginAPI.h)
- [CommonLib format-5 reader](https://github.com/alandtse/CommonLibSSE-NG/blob/8eaddedd1542290701ccb0e9e88b2af8b3cea597/src/REL/IDDB.cpp)
- [Player layout and versioned accessors](https://github.com/alandtse/CommonLibSSE-NG/blob/8eaddedd1542290701ccb0e9e88b2af8b3cea597/include/RE/P/PlayerCharacter.h)
- [VM layout and versioned accessors](https://github.com/alandtse/CommonLibSSE-NG/blob/8eaddedd1542290701ccb0e9e88b2af8b3cea597/include/RE/S/SkyrimVM.h)
- [Player input virtual slots](https://github.com/alandtse/CommonLibSSE-NG/blob/8eaddedd1542290701ccb0e9e88b2af8b3cea597/include/RE/P/PlayerInputHandler.h)

We use hand-written bindings rather than linking against CommonLib. Updating the reference checkout or SKSE alone
will not update these layouts in our DLL. The networking, host authority and game rules do not currently show a need
for an architectural rewrite; engine bindings and validation are the expected concentration of work. That is an
assessment, not proof of unchanged game behavior.

## Checks on the actual 1.7.104 executable

These measurements do not depend on the 1.7.99 Address Library:

| RTTI subobject | 1.6.1170 offset | 1.7.104 offset |
|---|---:|---:|
| PlayerCharacter PositionPlayerEvent source | `0x388` | `0x390` |
| PlayerCharacter BSSystemEvent sink | absent | `0x2D8` |
| SkyrimVM PositionPlayerEvent sink | `0x190` | `0x1A0` |
| SkyrimVM SkyrimScript::StatsEvent source | `0x1A8` | `0x1B8` |
| SkyrimVM Amiibo touch / stop sinks | absent | `0x180` / `0x188` |

I located StatsMenu::ProcessMessage through RTTI and vtable slot 4 in both binaries (`0x95F710` -> `0x976A50`).
The **exact patched spans** at `+0x84E` (6 bytes), `+0xA10` (4 bytes), and `+0x1040` (2 bytes) are unchanged. Disassembly
still shows the expected conditional branch, freeze-background flag write and conditional branch. This is encouraging
evidence that some existing patch logic can carry over. It validates those three sites only; no blanket claim is made
about the remaining patches, surrounding function behavior, or runtime gameplay.

## Moving the IDA references

Keep the 1.6.1170 database intact and build a **separate** `SkyrimSE_1.7.104.i64` from the private unpacked executable.
The existing workflow is useful, but needs these changes:

1. Parameterize the executable version, library, SKSE source and output database in `make_ida_db.sh` and
   `build_re_data.py`. Currently they hardcode 1.6.1170; relabeling a different executable with those filenames would
   silently mix versions.
2. Add the v5 decoder to reference generation and export. Widen the ID inventory, refresh community references and
   preserve the version of each source. Current CommonLib uses forms such as `AE_CHECK(...)` and `VariantID(...)`
   that the existing two-integer regex does not fully understand.
3. Transfer note **names by stable ID**, resolving the RVA from the target library. All 234 notes that resolve in the
   1.7.99 check moved RVA. Never apply the existing `annotations.json` to the new binary simply because the image base
   is the same. `ida_apply.py` currently checks the base but does not enforce the executable version/hash.
4. Preserve evidence in comments with its source version. Old instruction offsets and old conclusions need review;
   a stable function ID does not make every offset inside its body stable. Handle the OK callback explicitly.
5. Export/import useful local types separately and audit changed layouts. `ida_export.py` only preserves names and
   selected comments at Address Library addresses; it does **not** preserve the full type library, local-variable
   work, interior instruction comments, or every function prototype. Unmapped work needs a function comparison or
   manual transfer. Retain version-specific notes and generated annotations for both runtimes.

Most of the saved names should transfer mechanically once the exact new library is present. IDA auto-analysis is
machine time; validating transferred types, signatures and patch sites is the engineering work. No new database was
created and the existing open database was not edited in this assessment.

## Proposed work order and effort

| Batch | Engineering time | Exit condition |
|---|---:|---|
| Pin exact 1.7.104 artifacts, decode/map IDs, parameterize RE tools, create new IDA database | 1–2 days | Reproducible inventory against the exact runtime; no unidentified required address |
| Correct startup, layouts, missing bindings, entry hooks and interior patches | 2–4 days | Supported runtime selected explicitly; patch/signature review complete; plugin can enter a game |
| Validate co-op scenarios, resolve regressions, update harness/package/diagnostics | 2–4 days | Fast gates pass and targeted plus combined native acceptance is recorded |
| Optional: support both runtimes in the same DLL | additional 2–5 days | Versioned typed field access and per-runtime patches tested on both versions |

Start with a separate 1.7.104 build while retaining the existing 1.6.1170 build and test installation. Decide on one DLL
versus two artifacts after the working port is measured; one DLL requires more care because ordinary C++ field access
does not change offsets when the runtime changes. Continue requiring matching runtime/SKSE within a co-op session
until mixed-runtime sessions are explicitly designed and tested.

Native acceptance should cover boot/new game, host/join/rejoin, movement/animations, combat and magic, inventory and
appearance/perks, shared rest, dialogue/menus, travel, race changes, physics and save/load. Race transformations deserve
attention because Bethesda explicitly lists a race-transformation crash fix. Do not use all-green fake games as proof
that external layouts and detours are correct. For each discovered behavioral bug, follow the project's order:
native scenario -> release duo -> failing GameTests/engine fact -> units and fix -> fast gate; rerun the native
scenarios together for acceptance.

## Evidence and scope

[Machine-readable evidence](2026-11-02-steam-runtime-port-evidence.json) records hashes, counts, missing IDs, RTTI
measurements and patch bytes. The reproducible offline audit script and full literal-ID inventory remain privately in
this worktree under `build/runtime-audit/audit.py` and `build/runtime-audit/inventory.json`; downloaded references and
Bethesda binaries are not committed.

No production code was changed; no game was launched, deployed or stopped; no duo claim was taken; no merge into dev
was performed. Compilation and `scripts/check.sh` were not run for this report-only change. The assessment script
ran successfully and checked the binary identities, fixture bounds, missing-ID inventory and matching patch spans.
