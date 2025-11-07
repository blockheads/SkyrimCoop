# Building with MSVC via Wine (Linux Docker)

This setup uses **mstorsjo/msvc-wine** to run the real Microsoft Visual C++ compiler on Linux through Wine. It produces genuine MSVC binaries that work with Skyrim.

## What This Is

- ✅ **Real MSVC compiler** (cl.exe, link.exe) from Microsoft
- ✅ **Real Windows SDK** headers and libraries
- ✅ **Wine** to run Windows binaries on Linux
- ✅ **XMake Windows version** running through Wine for native Windows package support
- ✅ **Produces genuine PE executables** compatible with Skyrim
- ✅ **Works on pure Linux** - no Windows required

## First-Time Setup

The Docker image build downloads ~2.7GB of Microsoft components (one time):

```bash
docker compose -f docker-compose.msvc-wine.yml build
```

This takes 10-15 minutes on first run.

## Building the Project

### Option 1: One-Command Build

```bash
./build-msvc-wine.sh
```

### Option 2: Manual Steps

```bash
# Start container
docker compose -f docker-compose.msvc-wine.yml run --rm msvc-wine bash

# Inside container:
./configure-msvc-wine.sh
xmake -y
xmake install -o distrib
```

## How It Works

```
┌──────────────────────────┐
│ Your C++ Code            │
└────────────┬─────────────┘
             │
             ↓
┌──────────────────────────────────────┐
│ Wine Environment                      │
│   ↓                                   │
│ xmake.exe (Windows)                   │  ← XMake running in Wine
│   ↓                                   │
│ Downloads/builds Windows packages     │  ← Native Windows packages
│   ↓                                   │
│ cl.exe (Microsoft)                    │  ← Real MSVC compiler
│   ↓                                   │
│ link.exe (Microsoft)                  │  ← Real MSVC linker
└────────────┬─────────────────────────┘
             │
             ↓
┌──────────────────────────┐
│ SkyrimTogether.dll       │
│ (Genuine MSVC binary)    │
└──────────────────────────┘
```

## Build Times

- **First build**: 20-30 minutes (downloads packages)
- **Incremental build**: 5-10 minutes
- **Full rebuild**: 15-20 minutes

## Technical Details

### What Gets Installed

The msvc-wine download script fetches:
- Visual C++ compiler toolchain (cl.exe, link.exe, lib.exe)
- Windows SDK headers (windows.h, etc.)
- UCRT (Universal C Runtime)
- MSVC CRT libraries
- ATL/MFC libraries (optional)
- Debug symbols (PDB generation)

### Wine Version

Uses Wine Stable from WineHQ repository:
- Runs 32-bit and 64-bit Windows executables
- Supports MSVC compiler features
- Handles PDB debug symbol generation

### XMake Integration

The build uses **XMake Windows version running in Wine**:
1. XMake for Windows is downloaded as a portable zip
2. A wrapper script (`/usr/local/bin/xmake`) runs `xmake.exe` through Wine
3. This ensures XMake downloads and builds **Windows packages** (not Linux ones)
4. The `build-with-msvc-wine.sh` script:
   - Sources `msvcenv.sh` from msvc-wine to set up MSVC environment
   - Sets up TEMP/TMP directories for Wine
   - Configures XMake to use MSVC toolchain via Wine paths (Z:\opt\msvc\...)
   - XMake builds packages and project code all within Wine environment

## Comparison with Other Approaches

| Approach | Binary Type | Works in Skyrim | Build Speed | Complexity |
|----------|-------------|-----------------|-------------|------------|
| **MSVC-Wine (this)** | Real MSVC | ✅ Yes | Medium | Medium |
| Native MSVC (Windows) | Real MSVC | ✅ Yes | Fast | Low |
| clang-cl | MSVC-like | ⚠️ Maybe | Medium | High |
| MinGW | MinGW | ❌ No | Fast | Low |

## Advantages

✅ **Pure Linux development** - No Windows VM/dual-boot needed
✅ **Docker reproducibility** - Same build everywhere
✅ **CI/CD ready** - Works in GitHub Actions, GitLab CI, etc.
✅ **Guaranteed compatibility** - Uses actual Microsoft compiler
✅ **Full debugging support** - Generates real PDB files

## Disadvantages

⚠️ **Slower than native** - Wine overhead (~30% slower)
⚠️ **Large download** - 2.7GB of Microsoft components
⚠️ **Wine dependency** - Requires Wine runtime
⚠️ **Memory usage** - Docker + Wine uses more RAM

## Troubleshooting

### Build fails with "wine: could not load kernel32.dll"

Wine initialization failed. Rebuild the Docker image:
```bash
docker compose -f docker-compose.msvc-wine.yml build --no-cache
```

### "error: toolchain not found"

The msvcenv script didn't run. Make sure you run:
```bash
./configure-msvc-wine.sh
```
Before running `xmake`.

### Slow compilation

This is normal with Wine. To speed up:
- Increase Docker resources (CPU/RAM)
- Use ccache (already configured)
- Enable unity build: `xmake config --unitybuild=y`

### "out of memory" errors

Increase Docker memory limit:
```bash
# In Docker Desktop: Settings → Resources → Memory
# Recommend: 8GB minimum, 16GB for comfortable builds
```

## License Considerations

**Microsoft's MSVC license**: The msvc-wine project downloads components from Microsoft's official servers. According to mstorsjo/msvc-wine documentation:

- ✅ Legal for personal use
- ✅ Legal for organizational use (if you have VS license)
- ⚠️ Check Microsoft's license terms for your use case
- ❌ Do not redistribute the MSVC files

The Docker image itself doesn't include redistributable files - they're downloaded during build.

## References

- [mstorsjo/msvc-wine](https://github.com/mstorsjo/msvc-wine) - MSVC on Wine scripts
- [WineHQ](https://www.winehq.org/) - Wine project
- [Microsoft Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) - Official MSVC
- [XMake](https://xmake.io/) - Build system
