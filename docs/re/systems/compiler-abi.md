# Client compiler: clang-cl (MSVC ABI), not MinGW

The client DLL is built from Linux with **clang-cl + lld-link** against the Windows SDK and
MSVC CRT fetched by xwin (`scripts/toolchain/setup_msvc_toolchain.sh`, then
`scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL`).

## Why not MinGW (GCC)

The engine is MSVC-compiled C++ and we call into it through hand-declared classes. GCC uses
the Itanium C++ ABI, which differs from MSVC exactly where we touch the engine:

| Difference | Effect in-game |
|---|---|
| A virtual destructor takes **2** vtable slots in GCC, 1 in MSVC | Every later virtual is off by one: `BindNativeMethod` (slot 0x18) called `SetCallableFromTasklets1` (0x19) and crashed at startup ([crash page](../crashes/2026-10-10-startup-crash.md)) |
| Tail padding reuse and bitfield layout | Struct members land at different offsets; `SKYRIM_STRUCT_ASSERT` had to be disabled under MinGW |
| Clang devirtualizes calls on members of known type | Calls went to `link_stubs.cpp` dummies instead of the engine (fixed for all compilers with `EngineVirtual()` in `Code/client/Utils.h`) |

With clang-cl all 199 struct-layout asserts pass, and the vtable layout is MSVC's.

## Things that were silently MinGW-only

These were written as `#ifdef __MINGW32__` but are really "loaded by SKSE" concerns, so they now
apply to every Windows build:

- `Code/client/Hooks/Memory.h`: unprotect game code pages before patching, and branch through
  SKSE trampolines (our DLL can be >2 GB from the game).
- `Code/base/allocator/AllocatorSymbolOverride.cpp`: lazy rpmalloc init (static initializers run before
  `rpmalloc_initialize`; error 998 on load).
- The game's CRT heap stays its own: upstream's launcher swapped it for rpmalloc before the game
  allocated, but SKSE loads us after CRT startup (the swap is deleted, 2026-10-22).
