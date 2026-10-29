# 2026-10-10: crash ~4s after plugin init (resolved)

**Resolved:** GCC/MinGW vtable ABI. A virtual destructor takes two Itanium vtable slots, so our
`BindNativeMethod` call (MSVC slot 0x18) landed on `SetCallableFromTasklets1` (0x19), confirmed in
IDA. Fixed by building the client with clang-cl ([compiler-abi](../systems/compiler-abi.md)). The
"VM inactive" guard tried below used a wrong offset (0x690 is a counter on 1.6.1170; `isFrozen` is
0x680) and was removed again: the client ticks every VM update, as before.

**2026-10-29 follow-up:** the frozen-VM check is now restored at the verified `0x680` offset
for a separate save-time re-entry defect. Its earlier removal did not resolve the compiler ABI
crash and must not be read as evidence that frozen updates are safe. See
[the save-drain investigation](2026-10-29-save-vm-reentry.md).

**Symptom:** `scripts/coop_test.py launch` (fresh `coc QASmoke` start, 1.6.1170, Proton 10,
SKSE 2.2.6). SKSE loads the plugin, `tp_client.log` reaches `SKSE is already active`, then:

```
VectoredExceptionHandler: crash occurred!
exception code is c0000005, at address 0x6fffffa4f0d0
```

Dumps: `crash_UTC_*.dmp` in the game root (written by `Code/client/CrashHandler.cpp`).

**What the dump shows**

- Faulting instruction: `ucrtbase!strlen` reading `0x26d930`, called from the CRT's printf
  `%s` handling, called from SkyrimSE `+0x1aa1660` (ID 252675, unnamed sprintf-style wrapper).
- Stack scan also shows Papyrus VM (`VirtualMachine::Func25`, ID 104790), `BSFixedString::Release`
  → `BSStringPool::Release` (69164 → 69192) and our `HookFormAllocate`. Stack scans include stale
  frames: ID 68432 turned out to be the small-block allocator's lock/alloc path, not a caller.
  **Next step: get a real call stack** (IDA/x64dbg on the dump, or winedbg attached).

**Ruled out**

| Hypothesis | Test | Result |
|---|---|---|
| Newly linked hook files (38) | Archive-linked DLL (old behaviour) | No crash, but plugin never ticks (main-loop hook dropped); inconclusive |
| VM natives allocated with DLL `new`, freed by game heap | `Memory::New` for them | Same crash (kept: correct anyway) |
| Client ticking while VM inactive (fork removed upstream guard; wrong `inactive` offset) | Restored upstream guard, offset 0x690 | Same crash (kept: correct anyway) |

**Still to try:** symbolised stack from IDA; bisect hook groups (disable `Initializer`s by file).
