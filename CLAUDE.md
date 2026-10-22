# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**SkyrimCoop**

A peer-to-peer cooperative multiplayer mod for Skyrim Special Edition, forked from Tilted Online (Skyrim Together Reborn). Converts the original MMO-style client-server architecture to a host-based co-op model (2-4 players, drop-in via Steam invite). The entire toolchain builds and debugs natively on Linux, cross-compiling the Windows client DLL with clang-cl (MSVC ABI) and xwin's Windows SDK.

**Core Value:** Two friends can drop into Skyrim together on Linux with zero server setup — host clicks "host," friend joins via Steam invite, and it just works.

**Direct authority:** the host's game is the only authority. It owns every NPC and the world; each guest owns only its own player. There is no server: the host's rules (`Code/client/Services/Host/`, `HostAuthority`) run inside the host's game and listen for the guests' games (`HostNetwork`). Where things stand: `docs/progress.md`; the words (host, guest, owner, copy, player): `docs/design/glossary.md`.

**License:** GNU GPLv3

### Constraints

- **Build system**: Cross-compile the Windows DLL from Linux with clang-cl + lld-link (MSVC ABI; MinGW/GCC breaks engine vtables). Setup `scripts/toolchain/setup_msvc_toolchain.sh`, build `scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL`. Details: `docs/re/systems/compiler-abi.md`
- **Testing**: `scripts/coop_test.py duo` (two Skyrim instances), `scenario` (two-game sync scenarios), `reload` (swap a rebuild into the running games); games run off-screen, see the script docstring
- **Test harness**: the command socket, control files, status and harness engine calls live in `Code/client/TestHarness/`, the DLL swap in `Code/client/HotReload/` and `Code/hotreload/Loader.cpp`. Both build only with the xmake option `test_harness` (`COOP_TEST_HARNESS`), which `scripts/xmake-msvc.sh f` turns on; a player build (`--test_harness=n`, the default elsewhere) contains neither. Production code reaches the harness only through `TestHarness::Configure/Poll/Shutdown`, each behind `#if COOP_TEST_HARNESS`
- **SKSE dependency**: Client DLL loads into Skyrim via SKSE — cannot modify SKSE itself
- **Wine/Proton**: Client always runs under Wine/Proton on Linux, debug tooling must work with this
- **License**: GPLv3 — all modifications must remain open source
- **Mod compatibility**: Host's mod list is authoritative; clients must match to connect
- **Platform**: Linux-first development, Windows compatibility maintained for end users

### Engineering rules

- **Test code never widens production interfaces.** No harness methods on the bridges, services or engine structs; the harness reads engine state through its own `TestHarness/EngineCalls`, and production code calls the harness only through the gated entry points.
- **Engine access goes through typed structs** in `Code/client/Games/` with `static_assert`ed offsets, checked against CommonLibSSE-NG and IDA. No raw byte offsets in services, bridges or the harness.
- **Before reversing anything, search GitHub/Nexus** for an SKSE mod that already does it, and add good ones to `scripts/re/fetch_references.sh`.
- **When adding or touching a hook,** check its signature against the community name and the disassembly (three inherited upstream hooks were silently wrong).
- **Scenarios drive the game as a player would** (input through the engine's dispatch) and wait for game state, never sleeps or retries. A failure names the step that went wrong.
- **Keep code comments short.** One or two lines saying what the code is for and the engine ID or name it relies on. The reasoning, evidence and history go in `docs/` (`docs/re/systems/`, `docs/design/`), not in the source.
- **Host, guest, every game.** Code only the host runs lives in `Services/Host/` (rules, `HostAuthority`) and `Bridges/HostEngine`; code only a guest runs in `Services/Guest/` and `Bridges/GuestEngine`, made by `GuestService` on joining. No `IsHosting()` checks inside them. Actors split by owner and copy instead.
- **Use the glossary's words** (`docs/design/glossary.md`): host and guest for the games, owner, local and copy for actors, player for a person in the party. New code and docs never say client, peer or remote player for a guest or a copy.

## Build

XMake 2.8.5+, C++23 (the Linux build needs `libstdc++-13-dev`, from the `ubuntu-toolchain-r/test` PPA on 22.04). Two configurations, each with its own config directory, one target per build, `-j8`
(`-j16` has crashed clang-cl and the linker with the duo running).

```bash
# The SKSE plugin: clang-cl targeting Windows (first time: scripts/xmake-msvc.sh f -m dev)
scripts/xmake-msvc.sh build -j8 SkyrimTogetherClientDLL
scripts/xmake-msvc.sh build -j8 SkyrimCoopLoader          # hot reload's resident loader

# Native Linux: clang 18 (first time: scripts/xmake-linux.sh f)
scripts/xmake-linux.sh build -j8 BridgeTests && build/linux/x86_64/debug/BridgeTests
scripts/xmake-linux.sh build -j8 TPTests && build/linux/x86_64/debug/TPTests
scripts/xmake-linux.sh build -j8 coop_peer

# clangd's compile database (both builds): build/compile_commands.json
scripts/compile_commands.sh
```

Targets:

- `SkyrimTogetherClientDLL` - the SKSE plugin (`SkyrimTogetherClient.dll`), every game's code, the host's rules included
- `SkyrimCoopLoader` - hot reload's loader (`Code/hotreload`), test builds only
- `SkyrimCoopVanillaHarness` - a stand-in plugin running the harness commands without ours (`coop_test.py repro --vanilla`)
- `BridgeTests` - the host's rules (`HostAuthority`, `Services/Host/`) with in-process games and no Skyrim (Catch2)
- `TPTests` - message encoding, quantization, differentials (Catch2)
- `coop_peer` - a game stand-in on the network for scenarios (a host refusing joins, a game asking to join)

## Layout

```
Code/
├── client/              # The SKSE plugin
│   ├── Services/        # Systems every game runs (Character, Inventory, Magic, Transport, ...)
│   │   ├── Host/        # The host's rules: HostAuthority, HostNetwork (the listener), sessions, characters, world state
│   │   └── Guest/       # Only while joined: the host's clock, weather, world state applied
│   ├── Bridges/         # The engine behind interfaces: SkyrimBridge, HostEngine (the host's reads), GuestEngine
│   ├── Games/Skyrim/    # Typed engine structs and hooks
│   ├── Hooks/           # MinHook wrapper (HOOK, Hooks::Initializer), patch helpers, D3D11/input hooks
│   ├── Components/      # ECS components (LocalComponent, RemoteComponent, ...)
│   ├── Systems/         # Interpolation, animation, rendering
│   ├── Ui/              # The in-game UI (RmlUi)
│   ├── TestHarness/     # What scripts/coop_test.py drives; test builds only
│   └── HotReload/       # Swapping a rebuilt DLL into a running game; test builds only
├── encoding/            # Messages (Messages/), shared structs (Structs/), the wire buffer (Wire/), opcodes
├── network/             # The enet transport (Net::Server under HostNetwork, Net::Client under TransportService)
├── common/, base/       # Build version, DateTime, NO_COPY_MOVE; rpmalloc as global new/delete, thread names
├── components/imgui/    # The debug views' renderer
├── hotreload/           # Hot reload's loader
├── vanilla_harness/     # The harness without our plugin
└── tests/               # TPTests (encoding.cpp), bridge/ (BridgeTests), peer/ (coop_peer)
```

### How a message flows

```
guest's game: a service sends a request (TransportService::Send)
  → enet → the host's HostNetwork → HostAuthority::Handle → its rule (Services/Host/)
  → notifications to the other games (HostPlayers::SendToOthers)
  → each game's service handles the notification
the host's own game: the same, over an in-process route instead of enet
```

### Adding a message

1. Define the struct in `Code/encoding/Messages/` (a `RequestMessage` or `NotifyMessage` with `SerializeRaw`/`DeserializeRaw`)
2. Add its opcode to `Code/encoding/Opcodes.h` and the message to `RequestMessageFactory.h` / `NotifyMessageFactory.h`
3. The host's rule: a part in `Services/Host/` registered with `HostAuthority::On` (or a `Relay`), with a `BridgeTests` case
4. Each game's handler: a client service's `m_messageHandlers` entry or dispatcher sink
5. A round trip in `Code/tests/encoding.cpp`

## Reverse Engineering Reference

- Start at `docs/re/README.md`: generated `docs/re/ENGINE_REFERENCE.md` lists every engine address (Address Library ID) the client uses with its CommonLibSSE/SKSE/meh321 name and hook/call usage; `docs/re/systems/` and `docs/re/crashes/` hold hand-written findings
- Regenerate after adding/changing `POINTER_SKYRIMSE` IDs: `python3 scripts/re/build_re_data.py` (references fetched by `scripts/re/fetch_references.sh` into git-ignored `reference/`)
- IDA database (not committed): `build/re/SkyrimSE_1.6.1170.i64`, built by `scripts/re/make_ida_db.sh`; sync with `scripts/re/ida_apply.py` / `ida_export.py`

## Animation Synchronization

- `AnimationSystem` interpolates copies' animations; `LocalAnimationComponent` / `RemoteAnimationComponent` track state
- Animation variables are sent with differential encoding
- Behavior mods via Nemesis/Pandora patches: `README-ANIMATION-MODS.md`

## Conventions

C++ formatting: `clang-format` (`Code/.clang-format`; it re-sorts includes, so format edits by hand and only run it on new
files). See also `CODE_GUIDELINES.md`.

Naming:

- Variables camelCase: `someVariableName`; arguments prefixed `a` (`aFunctionArgument`), consts `c` (`cSomeInt`),
  pointers `p` (`pSomePointer`), combined (`acpSomeArgument`)
- Statics `s_`, globals `g_`, members `m_` (`m_pSomeMemberPointer`)
- Types and functions PascalCase: `struct TransportService`, `bool IsOnline()`
- Dispatcher handlers prefixed `Handle` or `On`; services suffixed `Service`, components `Component`, events `Event`,
  messages `Request` / `Response` / `Notify...`

Code:

- No exceptions: return `bool` or result values, `noexcept` where possible, `[[nodiscard]]` on results that matter
- Pass complex objects by const reference (`const AuthenticationResponse& acMessage`)
- `struct` for most types; `NO_COPY_MOVE` in services; `#pragma once` in every header
- Include what a file uses. `Code/client/ClientPch.h` is forced into every client file and holds only third-party,
  standard and rarely edited headers of ours (editing one rebuilds the whole plugin); never add a service,
  component or engine struct there
- Log with spdlog (`spdlog::info/warn/error/debug`), which formats with `std::format` (format strings checked at compile
  time); `std::format` for strings, never `fmt::`
