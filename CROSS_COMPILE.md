# Cross-Compiling SkyrimCoop from Linux

Build SkyrimCoop on Linux/macOS targeting Windows using clang-cl.

## Prerequisites

- Docker and Docker Compose
- At least 8GB RAM available to Docker
- 20GB+ disk space (Windows SDK is large)
- Git (for applying patches)

## Quick Start

### 1. Build the Docker image (first time only, ~15-20 minutes)

```bash
docker-compose -f docker-compose.dev.yml build
```

This downloads and installs:
- Clang/LLVM 17 with clang-cl support
- Windows 10 SDK headers and libraries
- XMake build system
- All cross-compilation tooling

### 2. Start the development container

```bash
docker-compose -f docker-compose.dev.yml run --rm skyrimcoop-dev
```

You'll be dropped into a bash shell inside the container. Your project directory is mounted at `/workspace`.

### 3. Build the project (inside container)

```bash
# Make build script executable (first time only)
chmod +x build-clang-cl.sh

# Build in debug mode (recommended for development)
./build-clang-cl.sh debug

# Or build in release mode
./build-clang-cl.sh release
```

The script will:
- Apply patches to disable CEF/UI (not needed for debugging)
- Configure XMake for clang-cl cross-compilation
- Build all targets except UI components
- Output to `build/windows/x64/{debug|release}/`

### 4. Copy built files to Windows for testing

From your **host machine** (not inside Docker):

```bash
# Copy the built DLL
cp build/windows/x64/debug/SkyrimTogether.dll /path/to/skyrim/Data/SKSE/Plugins/
```

Then test on Windows with Skyrim Special Edition.

## What Works

✅ Core networking (NetworkBridge, NetworkClient, GameNetworkingSockets)
✅ All game services (CharacterService, InventoryService, etc.)
✅ SKSE plugin compilation
✅ P2P hosting and connecting
✅ Server::World and client World
✅ Debug builds for development

## What's Disabled

❌ **CEF UI** (Angular web overlay)
- Not needed for core development
- Use console logging (`spdlog`) instead
- F9/F6 hotkeys still work for host/join
- Can reimplement with Dear ImGui or web UI later

❌ **tp_process** (CEF worker process)
- Only needed for rendering UI
- Removed from build

❌ **DirectXTK** (optional graphics helper)
- Not needed for core functionality

## Testing Requirements

**You MUST use Windows to test:**
- Run Skyrim Special Edition
- Load SKSE with your built DLL
- Verify game integration
- Test multiplayer connectivity

This setup **only** enables building on Linux. You cannot run Skyrim on Linux (yet - Proton/Wine testing TBD).

## Development Workflow

Recommended workflow for Linux developers:

1. **Edit code** on your Linux host (use any editor/IDE)
2. **Build** inside Docker container with `./build-clang-cl.sh debug`
3. **Copy** `SkyrimTogether.dll` to Windows machine (via network share, USB, etc.)
4. **Test** in Skyrim on Windows
5. **Iterate** - repeat steps 1-4

Alternatively, use WSL2 on Windows for best of both worlds.

## Troubleshooting

### Build fails with "clang-cl: command not found"

Verify clang-cl is installed:
```bash
which clang-cl
clang-cl --version
```

Should show `/usr/bin/clang-cl` and version 17+. If missing, rebuild Docker image.

### Build fails with Windows SDK errors

Check Windows SDK installation:
```bash
ls /opt/winsdk
# Should show: crt/, sdk/, um/, shared/
```

If missing or incomplete:
```bash
xwin --accept-license splat --output /opt/winsdk
```

### Linker errors about missing symbols

Some dependencies may not link properly with clang-cl. Check error message for which library, then:
1. Try disabling that dependency (if optional)
2. Check if there's a source-based alternative
3. File an issue with details

### DLL loads but crashes in Skyrim

This indicates potential ABI incompatibility:
- Check compiler warnings during build
- Compare symbol exports with MSVC-built version
- Verify clang-cl is using MSVC compatibility mode
- May need to use actual MSVC on Windows

### XMake configuration fails

Try manual configuration:
```bash
xmake f --plat=windows --arch=x64 --toolchain=clang-cl --sdk=/opt/winsdk -m debug -c -y -v
```

Add `-v` for verbose output to see what's failing.

## Advanced Usage

### Keep container running for faster builds

```bash
# Start detached
docker-compose -f docker-compose.dev.yml up -d

# Exec into it multiple times
docker exec -it skyrimcoop-dev bash

# Rebuild without restarting container
./build-clang-cl.sh debug

# When done
docker-compose -f docker-compose.dev.yml down
```

### Enable verbose build output

```bash
# Inside container
xmake -v -D
```

### Clean build

```bash
xmake clean
xmake f -c  # Reconfigure
xmake -y
```

### Disable additional dependencies

Edit `patches/disable-cef.patch` to remove more packages, or create a new patch.

## Known Limitations

1. **CEF cannot be cross-compiled** easily (prebuilt MSVC binaries)
2. **Discord SDK** may have issues (also prebuilt)
3. **First build is slow** (~20 minutes for Docker image + Windows SDK)
4. **Testing requires Windows** (no way around this)
5. **ABI compatibility** not 100% guaranteed (but should work)

## Future Improvements

- [ ] Test ABI compatibility thoroughly
- [ ] Investigate Dear ImGui as CEF replacement
- [ ] Add CI/CD GitHub Actions workflow
- [ ] Optimize Docker image size
- [ ] Support incremental builds better
- [ ] Test with Wine/Proton for Linux Skyrim

## Resources

- [XMake Cross-Compilation](https://xmake.io/#/guide/project_examples?id=cross-compilation)
- [clang-cl Documentation](https://clang.llvm.org/docs/MSVCCompatibility.html)
- [xwin Tool](https://github.com/Jake-Shadle/xwin)
- [LLVM Windows Support](https://llvm.org/docs/GettingStartedVS.html)
