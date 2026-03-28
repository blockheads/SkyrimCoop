---
status: diagnosed
phase: 03-client-dll-cross-compilation
source: [03-01-SUMMARY.md, 03-02-SUMMARY.md, 03-03-SUMMARY.md, 03-04-SUMMARY.md]
started: 2026-03-27T20:45:00Z
updated: 2026-03-27T21:05:00Z
---

## Current Test

[testing complete]

## Tests

### 1. MinGW DLL Build
expected: Run `xmake build SkyrimTogetherClientDLL` produces `SkyrimTogetherClient.dll` as PE32+ executable (DLL) x86-64.
result: pass

### 2. SKSE Export Symbols
expected: Running `x86_64-w64-mingw32-objdump -p` shows `SKSEPlugin_Load` and `SKSEPlugin_Version` in the export table.
result: pass

### 3. Smoke Test Auto-Detection
expected: find command returns a non-empty path to the built DLL.
result: pass

### 4. Static Library Unaffected
expected: `libSkyrimTogetherClient.a` still exists at ~1.5GB.
result: pass

### 5. DLL Loads in Skyrim via SKSE
expected: Copy DLL to SKSE Plugins, launch Skyrim SE under Proton via MO2. Plugin loads without crashes.
result: issue
reported: "SKSE error dialog: couldn't load plugin (000003E6). Error 0x3E6 = ERROR_NOACCESS — access violation during DLL load. First attempt was 0x7E (module not found) due to MinGW runtime DLL deps; fixed by static linking. Second attempt crashes during static initialization because --noinhibit-exec creates null pointers for unresolved Skyrim engine symbols."
severity: blocker

### 6. Client Connects to Server
expected: With the MinGW-built DLL loaded, start a local SkyrimTogetherServer. Client connects.
result: blocked
blocked_by: prior-phase
reason: "Cannot test — DLL fails to load (Test 5 blocker)"

## Summary

total: 6
passed: 4
issues: 1
pending: 0
skipped: 0
blocked: 1

## Gaps

- truth: "DLL loads into Skyrim SE via SKSE under Proton without crashing"
  status: failed
  reason: "User reported: SKSE error 0x3E6 (ERROR_NOACCESS). The --noinhibit-exec linker flag creates a DLL with null function pointers for unresolved Skyrim engine symbols. When C++ static constructors run during DLL_PROCESS_ATTACH, they hit these null pointers and crash. Need comprehensive stubs for ALL unresolved game engine symbols, or restructure linking to avoid pulling in code that references runtime-resolved symbols."
  severity: blocker
  test: 5
  artifacts:
    - Code/client/link_stubs.cpp
    - Code/client/xmake.lua
  missing:
    - "Complete stubs for all unresolved Skyrim engine symbols (not just ActorValueOwner/IAnimationGraphManagerHolder)"
    - "Or alternative: remove --noinhibit-exec and use proper symbol resolution strategy"
