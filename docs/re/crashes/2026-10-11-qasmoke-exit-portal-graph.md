# 2026-10-11: crash leaving QASmoke with an NPC placed (engine bug, fixed by us)

**Symptom:** `coc Riverwood` from QASmoke, with an NPC placed there (`player.placeatme 000A2C8E`, as
the scenario baseline does), crashes about 2s later. Repro: `scripts/coop_test.py repro
qasmoke-exit` (every run crashes, ~16s each).

**Verdict:** the engine's. `repro qasmoke-exit --vanilla` (SKSE, CrashLogger and the stand-in
harness plugin only) crashes the same way, 2 of 2. An earlier hand-typed vanilla run did not, which
sent the investigation after our plugin for a while: a hand-typed run is not the same test.

## Mechanism

1. Each frame, `Main::Update` refreshes the camera's `BSPortalGraphEntry` (persistent, allocated at
   boot) from the current cell: `TES::sub_13408` (ID 13408) fills its room array.
2. `coc` later in the same `Update` unloads QASmoke: `TESObjectCELL::sub_19056` →
   `TESObjectREFR::Set3D` → `ExtraPortal::dtor` → `BSPortal::dtor` frees the cell's portals.
3. The same frame's `Main::Draw` (ID 36559) runs the culling job `DrawWorld_BuildSceneLists`
   (ID 36643) with the room array from step 1. It walks a QASmoke room's `portalList`
   (`BSMultiBoundRoom+0x138`, `NiTPointerList<BSPortal*>`) and reads a freed portal's `+0x128`.

Whether the stale portal gets dereferenced depends on what reused its memory, so it is usually a
crash on garbage (`lock inc [rbx+0x08]` at `SkyrimSE.exe+64D207`, `rbx = 0x0119011801170116`, mesh
index data).

## How the tools got there

| Step | Showed |
|---|---|
| Plain report | access violation in culling, garbage register: a use after free seen too late |
| `--heap-history` | a 0x130-byte `BSPortal` freed by the cell unload 1.7s before |
| `--follow` | crash addresses repeat run to run; the graph entry and culling process live from boot |
| `--quarantine` | no crash: the stale portal is read and compared, not dereferenced through poison |
| `--guard 0x130` | faults at the stale read itself (`DrawWorld_BuildSceneLists+0x7CF`), freed by `ExtraPortal::dtor` |
| Hook switch (`SKYRIMCOOP_DISABLE_HOOKS`) | still crashes with only the allocator, main-loop and Papyrus hooks |
| `SKYRIMCOOP_CONSOLE_PHASE=ui`, recipe `qasmoke-exit-console` | still crashes when `coc` runs as typed, console open |
| `--probe 13408,36559,19056,...` | room refresh at 1644 ms, unload at 1634, `Draw` at 16 ms: the same frame, no refresh between |
| `--vanilla` | crashes without our plugin |

## Fix

`Code/client/Games/Skyrim/Fixes/PortalGraphFix.cpp` hooks `ShadowSceneNode::SetPortalGraph`
(ID 106385, called by the unload). When the graph changes, it empties the camera entry's room list
(`BSPortalGraphEntry+0x18`, releasing the rooms), clears its visibility (ID 76119) and resets its
graph and root room, as `BSPortalGraphEntry::SetPortalGraph` does. The next frame's refresh fills it
from the new cell. The entry is `*(*(ID 415022) + 0x30190)`, checked by its vtable (ID 238263).
`repro qasmoke-exit`: 0 of 3 crash after it (every run before), the player arrives in Riverwood.

Any cell change made mid-frame from an interior with portals hit this: `coc`, and possibly our own
teleports (`MoveTo` across cells).

[SCN-Crash-fix](https://github.com/InTheBottle/SCN-Crash-fix) (GPL-3, the Nexus "Shadow Scene Node
Crash Fix") fixes neighbouring cases in the same structures: lights deleted while still listed by
the scene node, rooms, portals and shared nodes, and portals still pointing at a deleted room. From
its source it does not touch this one (a live room listing a deleted portal through the camera
entry); not run against the repro. Its
member offsets (`RelocateMember(obj, SE/AE, VR)`) confirm the ones here: room `portalList` 0x138,
portal rooms 0x118/0x120, portal shared node 0x128.
