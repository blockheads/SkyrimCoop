# Memory

## Game heap hooks (`Code/client/Games/Memory.cpp`)

| ID | Engine function | Our use |
|---|---|---|
| 400188 | `gMemoryManager` | `GameHeap::Get()` |
| 68115 | `MemoryManager::Allocate` (CommonLib `RE/M/MemoryManager.h`) | **hooked** by `HookFormAllocate`: grows `Actor`/`PlayerCharacter` allocations to fit our `ActorExtension` |
| 68117 | `MemoryManager::Deallocate` | called by `Memory::Free` |

- 68115 is the general game allocator, not just forms: every engine allocation passes through
  `HookFormAllocate`, so it must stay cheap and must not allocate itself.
- `Memory::New<T>(args...)` allocates on the game heap. Use it for any object the engine will
  free (Papyrus native functions, engine-owned containers). It used to allocate twice and leak
  one block; fixed 2026-10-10.
- Upstream (launcher build) also replaces the CRT `malloc`/`free` with mimalloc through IAT hooks
  before the game starts. The SKSE build must **not**: SKSE loads us after CRT startup, so mixing
  allocators would free game memory with the wrong heap (`#ifndef __MINGW32__` in `Memory.cpp`).
- Engine small-block allocator: `BSSmallBlockAllocator` (IDs around 68432) takes a lock and
  serves allocations ≤ 0x200 bytes. A crash with `snprintf`/`strlen` under it in a stack scan is
  usually a stale frame, not the cause (see [crashes/2026-10-10-startup-crash.md](../crashes/2026-10-10-startup-crash.md)).
