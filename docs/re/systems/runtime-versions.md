# Steam runtime builds

SkyrimCoop primarily supports **1.7.104** with SKSE **2.3.1** and Address Library **v13**. The default xmake
`skyrim_version=latest` selects that exact executable. **1.6.1170** remains a separate build with SKSE **2.2.6**.
Each binary advertises and checks its exact runtime; the launcher, resident loader and vanilla harness match the DLL.

```bash
# Primary build
scripts/xmake-msvc.sh f -m dev
scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL

# Legacy configuration and artifacts are separate; select the option on both commands.
scripts/xmake-msvc.sh f -m dev --skyrim_version=1.6.1170
scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL --skyrim_version=1.6.1170

# The same selector applies to staging, packaging and RE tools.
COOP_SKYRIM_VERSION=1.6.1170 python3 scripts/coop_test.py duo --keep-running
COOP_SKYRIM_VERSION=1.6.1170 scripts/package.sh
COOP_SKYRIM_VERSION=1.6.1170 python3 scripts/re/build_re_data.py
```

The normal output is `build/windows/x64/dev`; legacy output is `build/skyrim-1.6.1170/windows/x64/dev`.
Native tests remain under `build/linux`. `COOP_SKYRIM_VERSION=1.6.1170` also selects the legacy Windows configuration
in the build wrapper. Packages include `-skyrim-<version>` in their names, use separate build directories, and verify
runtime-specific vendor manifests. The legacy manifest is `scripts/package-vendor-1.6.1170.sha256`.

## Staging

Keep Steam's installation read-only. The harness selects a matching `SkyrimSE.exe` or saved
`SkyrimSE_<version>.exe` sibling by PE file version and copies it into its own game directory. `SKYRIM_EXECUTABLE`
can name an explicit source. SKSE's loader, selected runtime DLL, scripts and the matching Address Library are staged
there too. A private `build/runtime-vendor/<version>/` takes precedence when present; otherwise the existing
`SKYRIM_SKSE_DIR` and `SKYRIM_ADDRESS_LIBRARY_DIR` overrides or the Steam installation supply them.
Never deploy or launch without the shared DUO reservation, including from an isolated worktree.

## Engine differences used by this port

| Binding | 1.6.1170 | 1.7.104 |
|---|---|---|
| Address Library file encoding | v2 delta records | v5 dense offsets, zero = absent |
| Player fields following the event bases | original offsets | +8 bytes |
| Player PositionPlayerEvent source | `0x388` | `0x390` |
| SkyrimVM implementation pointer | `0x200` | `0x210` |
| SkyrimVM frozen state | `0x680` | `0x690` |
| SleepWaitMenu OK callback ID | `52488` | `524025` |
| BookMenu ProcessMessage menu-mode call | `51049+0x6F` | `51049+0x71` |

The player and VM layouts have compile-time offset assertions. NativeReach's perk-point store classifier uses the
typed player offset too. The BookMenu patch checks for a direct call to ID `35406` before replacing it. The initial
upstream reference is [TiltedEvolution #885](https://github.com/tiltedphoques/TiltedEvolution/pull/885); the wait callback,
book call-site movement and our additional player fields were checked in the actual 1.7.104 executable.

## IDA and reference data

Keep `SkyrimSE_1.7.104.i64` and `SkyrimSE_1.6.1170.i64` separate under `build/re`. Generated data belongs in
`build/re/<version>/annotations.json`. The importer checks both the runtime and input-file SHA-256 before changing
anything. Stable-ID notes from `ida_notes.tsv` carry their 1.6.1170 provenance; new/revalidated notes are exported
to `ida_notes_1.7.104.tsv`. Local instruction offsets in historical comments require review.

`ENGINE_REFERENCE.md` describes the primary runtime; `ENGINE_REFERENCE_1.6.1170.md` retains the legacy index.
Direct `VersionDbPtr` declarations and RTTI registrations are included, and runtime-specific hook branches are
selected before scanning. Crash tools select the matching annotation set from the recorded Skyrim version.

The decoder tests cover sparse v5 IDs, v2 deltas, wrong runtimes and truncation. Offline comparison of the production
C++ reader with the Python reader agreed on all **435,162** 1.7.104 mappings and **428,461** 1.6.1170 mappings.
Three unused RTTI IDs (`392519`, `394235`, `398437`) were already absent on both runtimes and remain absent.

## Acceptance

[Port acceptance](../../../playtests/2026-11-02-steam-runtime-port.md) records the full fast gate and native results:
1.7.104 passed boot/join plus all 10 targeted scenarios; legacy passed boot/join and 9/10 (baseline physics tolerance).
Reviewed engine lock signatures are runtime-specific; live overlaps remain failures.
