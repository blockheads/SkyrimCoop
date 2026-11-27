# Steam runtime builds

SkyrimCoop supports only **1.7.104** with SKSE **2.3.1** and Address Library **v13** (format 5). 1.6.1170 support
was removed on 2026-11-26. Each binary advertises and checks its exact runtime; the launcher, resident loader and
vanilla harness match the DLL.

```bash
scripts/xmake-msvc.sh f -m dev
scripts/xmake-msvc.sh build -j8 SkyrimCoopPlugin
```

The output is `build/windows/x64/dev`; native tests are under `build/linux`. Packages include `-skyrim-<version>` in
their names. They hold only our mod; SKSE and the other vendor mods reach players through SkyrimModSync.

## Staging

Keep Steam's installation read-only. The harness selects a matching `SkyrimSE.exe` or saved
`SkyrimSE_1.7.104.exe` sibling by PE file version and copies it into its own game directory. `SKYRIM_EXECUTABLE`
can name an explicit source. SKSE's loader, selected runtime DLL, scripts and the matching Address Library are staged
there too. A private `build/runtime-vendor/<version>/` takes precedence when present; otherwise the existing
`SKYRIM_SKSE_DIR` and `SKYRIM_ADDRESS_LIBRARY_DIR` overrides or the Steam installation supply them.
Never deploy or launch without the shared DUO reservation, including from an isolated worktree.

## Engine differences from 1.6.1170 (history: where the 1.7.104 offsets came from)

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

The database is `build/re/SkyrimSE_1.7.104.i64`; generated data belongs in `build/re/1.7.104/annotations.json`. The
importer checks both the runtime and input-file SHA-256 before changing anything. Stable-ID notes in `ida_notes.tsv`
were read on 1.6.1170 and are frozen: `build_re_data.py` still loads them, marked "From 1.6.1170 (instruction offsets
require review)"; new and revalidated notes go to `ida_notes_1.7.104.tsv`.
