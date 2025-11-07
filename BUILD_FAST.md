# Fast Build Guide

## Optimized Build Commands

### Docker/MinGW Build (Linux → Windows)

**IMPORTANT**: Always use `-j` flag to enable parallel compilation!

```bash
# Build Docker image (first time only)
docker-compose -f docker-compose.dev.yml build

# Run optimized build
docker-compose -f docker-compose.dev.yml run --rm skyrimcoop-dev bash

# Inside container:
xmake config -p mingw -m debug -y
xmake -j8  # <-- THIS IS CRITICAL! Use all 8 cores

# For clean rebuild:
xmake clean
xmake -j8

# Or use auto-detect cores (recommended):
xmake -j$(nproc)  # Use all available CPU cores
```

### Local Windows Build

```powershell
# Quick debug build
xmake config -m debug -y
xmake -j%NUMBER_OF_PROCESSORS%  # Use all available cores

# With unity build (even faster):
xmake config -m debug --unitybuild=y -y
xmake -j%NUMBER_OF_PROCESSORS%
```

### Build Performance Tips

1. **Always use `-j8` or `-j$(nproc)`** - This enables parallel compilation
   - Without `-j`, XMake only uses 1 CPU core!
   - `-j` must be followed immediately by number (no space): `-j8` not `-j 8`
2. **First build will be slow** - ccache needs to populate
3. **Subsequent builds will be 5-10x faster** - ccache kicks in
4. **Use `xmake -v -j8` to see what's taking time** - Verbose output for debugging
5. **Fixed linker errors** - Now using proper `-Wl,` prefix for linker flags

### Expected Build Times (MinGW Debug, 8 cores)

**Note:** MinGW cross-compilation from Linux is inherently slower than native Linux builds due to Windows compatibility layers. These times are normal:

- **First build**: 15-25 minutes (normal for large C++20 project)
- **Incremental build**: 1-3 minutes (ccache + parallel)
- **Clean rebuild**: 10-15 minutes (ccache hit rate ~80%)

**Key insight from research:** MinGW on Windows is 4-5x slower than the same GCC version on Linux. Cross-compiling from Linux (Docker) is still faster than MinGW on Windows, but slower than native Linux builds.

### Latest Optimizations Applied

✅ **`-O0` for debug builds** - No optimization, fastest compilation
✅ **`-fno-inline`** - Don't inline functions in debug
✅ **Linker memory flags** - `--no-keep-memory`, `--reduce-memory-overheads`
✅ **All previous optimizations** - `-g1`, no warnings, no var tracking, parallel build

### If Still Slow

**IMPORTANT:** If the build is still taking 20+ minutes for first build or 5+ minutes for incremental, this is likely **normal for MinGW cross-compilation**. Research shows MinGW is inherently 4-5x slower than native Linux builds.

**Alternative solutions:**
- Use `releasedbg` mode instead of `debug` (faster compile, still has debug symbols)
- Build only the server: `xmake build SkyrimTogetherServer -j8`
- Accept the build time and let ccache work (subsequent builds will be much faster)

1. Check ccache is working:
   ```bash
   ccache -s  # Show cache statistics
   ```

2. Profile the build:
   ```bash
   xmake -v -j8 2>&1 | tee build.log
   # Look for slow targets in build.log
   ```

3. Use release mode instead:
   ```bash
   xmake config -p mingw -m releasedbg -y
   xmake -j8
   ```

4. Skip tests/launcher (server only):
   ```bash
   xmake build SkyrimTogetherServer -j8
   ```

5. Check if LLD linker is working:
   ```bash
   xmake -v -j8 2>&1 | grep -i "lld\|ld.lld"
   # Should see "ld.lld" being used instead of "ld"
   ```
