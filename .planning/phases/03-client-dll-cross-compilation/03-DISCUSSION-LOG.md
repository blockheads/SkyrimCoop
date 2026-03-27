# Phase 3: Client DLL Cross-Compilation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-27
**Phase:** 03-client-dll-cross-compilation
**Areas discussed:** CEF/DirectXTK exclusion strategy, MinHook/Xbyak cross-compile approach, DLL load validation scope, Client code MSVC-isms cleanup

---

## CEF/DirectXTK Exclusion Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Compile-time exclusion | Use preprocessor guards (#ifdef HAS_CEF) around all CEF/DirectXTK code paths. Client compiles and loads but has NO UI overlay. | ✓ |
| Strip and stub | Remove dependencies entirely, create minimal stub implementations | |
| Keep CEF, cross-compile it | Attempt to get CEF building under MinGW | |

**User's choice:** Compile-time exclusion
**Notes:** None — straightforward choice given Phase 6 replaces CEF with ImGui anyway.

### Discord SDK Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Exclude Discord SDK | Guard with #ifdef HAS_DISCORD, DiscordService becomes no-op | ✓ |
| Keep and cross-compile | Attempt to link MSVC-compiled Discord .lib with MinGW | |

**User's choice:** Exclude Discord SDK

### Guard Style Sub-question

| Option | Description | Selected |
|--------|-------------|----------|
| Per-feature flags | HAS_CEF, HAS_DISCORD, HAS_DIRECTXTK as separate defines | ✓ |
| Single umbrella flag | MINIMAL_CLIENT or NO_UI_OVERLAY as one flag | |

**User's choice:** Per-feature flags

---

## MinHook/Xbyak Cross-Compile Approach

### MinHook

| Option | Description | Selected |
|--------|-------------|----------|
| Cross-compile as-is first | Try building with MinGW first, patch specific issues if needed | ✓ |
| Fork and patch preemptively | Fork into repo and proactively replace MSVC-isms | |
| Replace with Detours or frida-gum | Switch to different hooking library | |

**User's choice:** Cross-compile as-is first

### Xbyak

| Option | Description | Selected |
|--------|-------------|----------|
| Cross-compile as-is first | Header-only, should compile with minimal issues | ✓ |
| Replace with AsmJit | More actively maintained JIT assembler | |
| You decide | Claude picks best approach | |

**User's choice:** Cross-compile as-is first

### Fallback Boundary

| Option | Description | Selected |
|--------|-------------|----------|
| Patch up to ~50 lines per lib | Small targeted patches, escalate if too large | |
| Unlimited patching effort | Spend as much effort as needed | |
| Zero patches — switch immediately | Don't compile = switch to alternatives | |

**User's choice:** (Other) "Try small patches — if it gets too large then reconsider alternatives." Judgment-based, not line-count-based.

---

## DLL Load Validation Scope

### Validation Depth

| Option | Description | Selected |
|--------|-------------|----------|
| Load + connect + basic sync | Match success criteria literally with character sync | |
| Load + connect only | DLL loads and establishes server connection, no sync required | ✓ |
| Load only (smoke test) | DLL loads without crashing, no networking | |

**User's choice:** Load + connect (adjusted down from recommended "load + connect + basic sync" to keep phase scope manageable)
**Notes:** User wanted to avoid increasing scope too much. Character sync validation deferred.

### Automation

| Option | Description | Selected |
|--------|-------------|----------|
| Manual testing is fine | Phase 3 is about compile and load, manual verification sufficient | |
| Add basic smoke test script | Script launches Skyrim, loads DLL, checks success markers | ✓ |

**User's choice:** Both — smoke test script AND manual testing. Smoke test catches initial bugs so manual testing is less painful.

### Smoke Test Scope

| Option | Description | Selected |
|--------|-------------|----------|
| DLL loads + SKSE log confirms init | Check SKSE log for plugin load confirmation | |
| DLL loads + connects to local server | Same plus spin up local server and verify connection | ✓ |
| You decide | Claude picks practical scope | |

**User's choice:** DLL loads + connects to local server

---

## Client Code MSVC-isms Cleanup

### Cleanup Approach

| Option | Description | Selected |
|--------|-------------|----------|
| Extend Phase 2 patterns | Same approach: portable C++20, __attribute__ fallbacks, delete MSVC blocks | ✓ |
| Minimal fixes only | Only fix what prevents compilation | |
| You decide | Claude picks cleanup depth | |

**User's choice:** Extend Phase 2 patterns

### Allocator

| Option | Description | Selected |
|--------|-------------|----------|
| Switch client to rpmalloc too | Consistent with Phase 2 decision | ✓ |
| Keep mimalloc for client | Client runs on Windows/Wine where mimalloc works | |
| You decide | Claude picks based on MinGW compat | |

**User's choice:** Switch to rpmalloc (consistency with Phase 2)

### Windows Syslinks

| Option | Description | Selected |
|--------|-------------|----------|
| Keep all — MinGW provides them | MinGW includes import libraries for standard Windows DLLs | ✓ |
| Guard dbghelp behind debug flag | Only needed for crash reporting | |
| You decide | Claude picks based on linking | |

**User's choice:** Keep all

---

## Claude's Discretion

- Specific MinHook/Xbyak patches if needed
- Smoke test script implementation details
- Order of client source file cleanup
- XMake configuration for per-feature guard macros

## Deferred Ideas

None — discussion stayed within phase scope
