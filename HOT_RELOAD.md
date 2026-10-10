# Hot reload development workflow

This implementation reloads **remote actor movement calculation** from
`Code/hotreload/MovementCore.cpp` and `MovementMath.h`. The existing SKSE plugin,
hooks, transport, server, actor creation and other services remain resident.
Changing those files still requires a rebuild and game restart. The DLL boundary
is stateless, so movement-code reloads preserve the active connection and actor.
This is the first step toward making more synchronization code reloadable.

## Build and install the resident plugin once

From the repository root:

```bash
xmake f -p mingw -m dev --mingw=.toolchain -y
xmake build -y -j16 SkyrimTogetherClientDLL
```

`dev` mode is the fast iteration build (-O1, line-table debug info, PCH, lld link):
a one-file change rebuilds the DLL in roughly 5-12s. Use `-m releasedbg` when you
need full debug info; it is much slower (~5 minute link). Keep `-j` around 16:
each client file needs ~1.2GB of RAM to compile.

With Skyrim closed, install `build/mingw/x86_64/dev/SkyrimTogetherClient.dll`
as `Data/SKSE/Plugins/SkyrimTogetherClient.dll` in your Skyrim installation. Keep
a backup of the old plugin. Do not run the abandoned `skyrim_coop_hooks.dll` relay
plugin alongside this full client; move that old plugin out of `Plugins` if it is
still installed. Keep the Address Library files.

Build the native network bot with a separate configuration directory so it does
not switch the resident plugin build away from MinGW:

```bash
XMAKE_CONFIGDIR=build/hotreload-native-config xmake f -p linux -m release -y
XMAKE_CONFIGDIR=build/hotreload-native-config xmake build -y -j4 fake_player
```

Launch Skyrim through **SKSE**, load a normal test save, and leave the game open.
The single-game helper uses your existing Proton prefix:

```bash
export SKYRIM_GAME_DIR="/path/to/Skyrim Special Edition"
export SKYRIM_PROTON_DIR="/path/to/Proton - Experimental"
export SKYRIM_COMPAT_DIR="/path/to/steamapps/compatdata/489830"
export SKYRIM_STEAM_RUNTIME_DIR="/path/to/SteamLinuxRuntime_4"
bash scripts/launch_dev_game.sh
```

Keep Steam running. Select the runtime required by your installed Proton version
(`toolmanifest.vdf` lists its Steam app ID); this installation's Experimental
requires Steam Linux Runtime 4.0. Running Proton directly can fail on older host
glibc versions before Skyrim starts.

A flat, quiet location is best: the scripted bot does not implement navigation or
physics. Use a normally created player/save so it has valid appearance data.

## Run a live session

In each terminal, point the tool at the actual plugin directory:

```bash
export SKYRIM_SKSE_PLUGINS="/path/to/Skyrim Special Edition/Data/SKSE/Plugins"
```

Start hosting and capture the current player's appearance, mod list and location:

```bash
python3 scripts/hot_reload.py host --timeout 90
```

This goes through the existing embedded server and real client authentication.
If either fails, the command reports a failure; it does not claim a network test
passed. The resulting snapshot is for this local development session only.

Start the movement watcher in another terminal:

```bash
python3 scripts/hot_reload.py watch
```

Start one bot through the actual ENet connection:

```bash
python3 scripts/hot_reload.py simulate --seconds 120
```

The bot authenticates with the captured mod list, registers its character, waits
for the assigned server ID, then sends movement near the player's captured
position. The bot deliberately fails if registration times out. This is a
movement simulation; equipment, combat and animation scenarios are not implemented.

Edit `Code/hotreload/MovementCore.cpp`, save, and watch the terminal. It builds a
small replacement DLL and waits for the running game's acknowledgement. A useful
manual demonstration is temporarily adding `output->position[0] += 40.f;` after
the `DefaultMovement` call, then removing it in the next build. Use an open test
area and watch for the position shift. The game PID must stay unchanged while
the generation and build ID change. Do not change the ABI header during a live
session; ABI layout changes require a version bump and resident-plugin restart.

To reload once, inspect status, or return to resident movement code:

```bash
python3 scripts/hot_reload.py reload
python3 scripts/hot_reload.py status
python3 scripts/hot_reload.py builtin
```

`status` includes the game's Windows/Wine PID, current build, generation, module
evaluation count and engine actor position readbacks. It rejects stale heartbeat
files. A loaded module with zero evaluations is not a working movement test.

With a bot moving, find its hexadecimal FormID in-game (or convert an actor key
from `status`, whose keys are decimal) and check real displacement:

```bash
python3 scripts/hot_reload.py observe --actor FF001234 --seconds 5
```

This requires fresh observations from the current module generation, loaded 3D,
matching requested/actual positions and actual displacement. Missing actors and
stationary actors fail. It checks engine position readback, not visual rendering,
animation correctness, collision or a complete two-player session.

## Automated loader checks

```bash
python3 tests/test_hot_reload.py
python3 tests/test_hot_reload.py --wine /opt/wine-stable/bin/wine
python3 tests/test_hot_reload_cli.py
```

The SKSE startup regression also exercises allocations before `main()`, engine
threads, executable hook memory, far jumps/calls and restoring code permissions:

```bash
python3 tests/test_allocator_startup.py --rpmalloc /path/to/mingw/rpmalloc/package
```

Use the MinGW package directory with `include/rpmalloc.h` and
`lib/librpmalloc.a`, not the native Linux package. Xmake's verbose compile command
for `RpmallocAllocator.cpp` shows the directory used by the client.

These test real shared-library replacement, unloads, concurrent calls, incompatible
ABI rejection, failed replacement recovery, invalid outputs and stale status.
They do **not** launch Skyrim or prove actor spawning works.

## Lifetime rules

The permanent loader owns its mutex and holds it across every module call and
replacement. The module exports a versioned C function table and returns plain
numeric values. It must not create threads, retain input pointers, register
callbacks, install hooks, throw exceptions across the ABI, or allocate state for
the resident plugin to destroy. Every publication gets a unique filename and is
published atomically after a successful build. The previous module stays active
if loading or ABI validation fails. Keep old DLLs until Skyrim exits; they are
also useful for debugging. This setup does not recover from arbitrary crashes or
memory corruption inside a replacement DLL.
