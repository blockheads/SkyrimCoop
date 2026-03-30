---
plan: "16-03"
status: complete
started: 2026-03-29
completed: 2026-03-29
---

# Plan 16-03: Relay DLL TESNPC::Create Pipeline — Summary

## What was built
Extended the relay DLL to support dual-path actor spawning:
- **FormId path** (existing): CMD_SPAWN_ACTOR with AI suppression added
- **Appearance path** (new): CMD_SPAWN_ACTOR_APPEARANCE via TESNPC::Create pipeline

## Key changes
- **hook_trampolines.h/cpp**: Added `GetIFormFactoryTable()` (addr 400508) for IFormFactory resolution
- **protocol.h**: Added `CMD_SPAWN_ACTOR_APPEARANCE` (0x8011), `HOOK_SPAWN_RESULT` (0x001F), renumbered `CMD_REMOVE_ACTOR` to 0x8012
- **json_protocol.h**: Added `kCmdSpawnActorAppearance` JSON op
- **relay_main.cpp**:
  - Base64 decoder for appearance buffers from JSON protocol
  - Thread-safe `AppearanceSpawnRequest` queue (TCP thread → game thread)
  - `SpawnActorFromAppearance()` implementing STR's Forms.cpp:116-127 + Actor.cpp:175-208
  - AI suppression (flag 0x02000000) on both spawn paths
  - DrainCommandQueue now drains appearance queue before fixed-size commands

## Deviations
- Adapted 16-03 plan's binary protocol approach to use existing JSON wire protocol (Phase 11)
- Appearance data received as base64-encoded JSON fields, decoded in relay DLL
- Manual integration required due to worktree conflicts with JSON protocol

## key-files
### created
- (integrated inline into existing files)

### modified
- Code/relay_dll/hook_trampolines.h
- Code/relay_dll/hook_trampolines.cpp
- Code/relay_dll/protocol.h
- Code/relay_dll/json_protocol.h
- Code/relay_dll/relay_main.cpp
