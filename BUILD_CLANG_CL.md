# Building with clang-cl in Docker (MSVC-Compatible)

This approach allows you to build Windows binaries that work with Skyrim from a Linux Docker container.

## How It Works

1. **xwin** downloads official Microsoft SDK headers and libraries
2. **clang-cl** compiles using MSVC ABI (same as Visual Studio)
3. **lld-link** links with MSVC-compatible linker
4. Output is a true Windows PE binary compatible with Skyrim

## Why This Works

- ✅ Uses real MSVC headers and libraries (downloaded via xwin)
- ✅ Clang's `-target x86_64-pc-windows-msvc` produces MSVC ABI
- ✅ Same struct layout as MSVC (no MinGW issues)
- ✅ Same calling conventions and exception handling
- ✅ No Wine required - native Linux toolchain

## Quick Start

```bash
# Build everything (first run downloads ~500MB of Microsoft SDK)
./build-clang-cl.sh

# Or manually:
docker compose -f docker-compose.clang-cl.yml build
docker compose -f docker-compose.clang-cl.yml run --rm clang-cl bash

# Inside container:
xmake f --toolchain=clang-cl -p windows -a x64 -m releasedbg -y
xmake -y
xmake install -o distrib
```

## Build Modes

```bash
# Debug build (faster compilation, larger binary)
xmake f --toolchain=clang-cl -p windows -m debug -y

# Release with debug symbols (recommended)
xmake f --toolchain=clang-cl -p windows -m releasedbg -y

# Full release
xmake f --toolchain=clang-cl -p windows -m release -y
```

## First Build Time

- Docker image build: ~5-10 minutes (downloads SDK)
- First compilation: ~15-20 minutes
- Incremental builds: ~2-5 minutes (with ccache)

## Comparison with Other Approaches

| Approach | MSVC ABI | Works in Skyrim | Build Speed | Complexity |
|----------|----------|-----------------|-------------|------------|
| **clang-cl (this)** | ✅ Yes | ✅ Yes | Medium | Low |
| Native MSVC | ✅ Yes | ✅ Yes | Fast | N/A (Windows only) |
| MinGW | ❌ No | ❌ No | Fast | Low |
| Wine + MSVC | ✅ Yes | ✅ Yes | Slow | High |

## Troubleshooting

### "Cannot find cl.exe"
The toolchain file should handle this, but if you see this error:
```bash
export CC=clang-cl
export CXX=clang-cl
```

### SDK download fails
xwin downloads from Microsoft servers. If it fails:
```bash
# Manually download in container
docker compose -f docker-compose.clang-cl.yml run --rm clang-cl bash
xwin --accept-license splat --output /opt/msvc
```

### Linking errors
Make sure you're using the clang-cl toolchain:
```bash
xmake f --toolchain=clang-cl -p windows -y
```

## Technical Details

### Struct Layout
Clang with `-target x86_64-pc-windows-msvc` uses:
- 8-byte default alignment (same as MSVC /Zp8)
- MSVC-compatible vtable layout
- MSVC-compatible name mangling
- SEH exception handling (Windows native)

### What Gets Downloaded
xwin downloads (~500MB):
- Windows SDK headers (windows.h, etc.)
- UCRT headers and libraries
- MSVC CRT headers and libraries
- Import libraries for kernel32.dll, etc.

### Why Not Just Use MSVC?
You can! This is for developers who want to:
- Build on Linux/WSL2
- Use Docker for reproducible builds
- Avoid Windows/Visual Studio license requirements
- Integrate with Linux CI/CD pipelines

## References

- [xwin](https://github.com/Jake-Shadle/xwin) - Download Microsoft SDK
- [clang-cl](https://clang.llvm.org/docs/MSVCCompatibility.html) - MSVC compatibility
- [XMake toolchains](https://xmake.io/#/manual/custom_toolchain)
