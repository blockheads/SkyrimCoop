---
status: awaiting_human_verify
trigger: "Relay DLL sends raw actor pointers over TCP to native client. Native client tries to dereference them via /proc/pid/mem which fails under Wine/Proton."
created: 2026-03-29T00:00:00Z
updated: 2026-03-29T00:00:00Z
---

## Current Focus

hypothesis: The protocol already sends formId and position data from DLL-side (Phase 9.1 upgrade visible in protocol.h arg layout comments). The native client CharacterService already reads formId from args[1] and position from args[2-4]. The remaining problem is in SyncMovement() which falls back to pread via GameReader for rotation reads and position reads when cache misses.
test: Trace all /proc/mem read paths in CharacterService and identify which ones are still active during normal operation
expecting: The rotation reads at lines 332-333 of CharacterService.cpp use m_reader.Read<float>() which goes through /proc/mem and fails under Wine
next_action: Identify all remaining pread/proc_mem paths that fire during normal actor sync and eliminate them

## Symptoms

expected: Native client receives actor formId and position data from the relay DLL over TCP and uses them to sync characters
actual: DLL sends raw pointers (e.g. 0x85D387A0), native client reads /proc/pid/mem to get formId but gets 0 for every actor. Hundreds of "CharacterService: ActorAdded but formId is 0" warnings flood the log. "Failed to read actor position" also floods.
errors: "SMOKE TEST FAIL: Could not find SkyrimSE.exe in /proc/1478532/maps", "Failed to read actor position at 0x85d387a0", "CharacterService: ActorAdded but formId is 0 at ptr=0x..." (hundreds of these)
reproduction: Run tests/launch_host.sh, load into Riverwood, observe native client logs
started: First ever test of the two-player system. The relay protocol was designed with /proc/mem reads but that approach doesn't work under Wine/Proton.

## Eliminated

## Evidence

- timestamp: 2026-03-29T00:01:00Z
  checked: protocol.h arg layout conventions (lines 52-89)
  found: Phase 9.1 already upgraded all hook opcodes to include DLL-resolved formId in args[1]. HOOK_SET_POSITION sends [refPtr, formId, posX_u64, posY_u64, posZ_u64]. HOOK_ACTOR_ADDED sends [actorPtr, formId].
  implication: The DLL IS already sending formId and position. The "formId is 0" errors must be coming from a different path.

- timestamp: 2026-03-29T00:02:00Z
  checked: hook_trampolines.cpp ReadFormId() helper (line 43-48)
  found: DLL reads formId at offset +0x14 directly from in-process memory (since it's loaded in Skyrim's address space). This works correctly.
  implication: DLL-side formId resolution is correct. The issue is on the native client side.

- timestamp: 2026-03-29T00:03:00Z
  checked: CharacterService.cpp HandleActorAdded (line 123-173) and OnHookEvent (line 57-119)
  found: HandleActorAdded receives formId directly from pArgs[1] (DLL-resolved). It does NOT use /proc/mem to read formId. The warn at line 129 fires when formId==0, which means the DLL sent formId=0.
  implication: The "formId is 0" warnings come from DLL sending 0 for formId during CHARACTER_CTOR hooks where formId isn't populated yet. This is documented in protocol.h line 60.

- timestamp: 2026-03-29T00:04:00Z
  checked: CharacterService.cpp SyncMovement() lines 310-416
  found: Lines 332-333 read rotation via m_reader.Read<float>(comp.gamePtr, kRotationXOffset) which goes through ProcMemReader. This fails under Wine. Also line 328 falls back to m_reader.ReadActorPosition(comp.gamePtr) when no cached position.
  implication: These are the active /proc/mem reads that produce "Failed to read actor position" errors.

- timestamp: 2026-03-29T00:05:00Z
  checked: game_reader.cpp ReadActorPosition() line 12-20
  found: Uses m_mem.Read() which calls pread() on /proc/pid/mem. Under Wine, the Windows process memory isn't directly accessible via Linux /proc/pid/mem.
  implication: Confirmed: pread fails under Wine for game memory. All data must come from DLL hooks.

## Resolution

root_cause: Two remaining /proc/mem-dependent code paths in CharacterService::SyncMovement(): (1) rotation reads via m_reader.Read<float>() at lines 332-333 which always use pread, and (2) position fallback at line 328 which uses pread when no HOOK_SET_POSITION event has been cached yet. Additionally, HOOK_SET_POSITION does not include rotation data, so there's no DLL-sourced rotation available. The "formId is 0" warnings are a separate (minor) issue from CHARACTER_CTOR hooks firing before formId is assigned.
fix: (1) DLL hook_trampolines.cpp: Added ReadRotation() helper, updated HookSetPosition and HookSpawnActor to send rotation data (rotX, rotY, rotZ) as args[5-7] over TCP. (2) Native CharacterService.cpp: Changed position cache from vec3 to struct{vec3 position, vec2 rotation}, updated HOOK_SET_POSITION and HOOK_SPAWN_ACTOR handlers to store rotation. Replaced /proc/mem-based rotation reads and position fallback in SyncMovement() with cache-only reads (skip actors with no cached data). (3) protocol.h: Updated arg layout comments. (4) main.cpp: Downgraded smoke test messages from error to warn (expected under Wine).
verification: Relay DLL compiles clean (xmake build SkyrimCoopHooksDLL). Native client not buildable under current mingw config but changes are syntactically minimal and follow existing patterns.
files_changed: [Code/relay_dll/hook_trampolines.cpp, Code/relay_dll/protocol.h, Code/native_client/Services/CharacterService.cpp, Code/native_client/Services/CharacterService.h, Code/native_client/main.cpp]
