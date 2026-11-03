# Main loop and Papyrus VM

How the client gets a per-frame tick, and what it does inside the Papyrus VM.

## Hooks (`Code/client/SkyrimVM64.cpp`)

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 36564 | `Main::Update` | `HookMainLoop` | Calls `BeginMain`/`EndMain` on the app. **SKSE hooks this too** (`Hooks_SaveLoad.cpp: ProcessEvents_Enter`). |
| 53926 | `SkyrimVM::ProcessRegisteredUpdates` | `HookVMUpdate` | Drives `Plugin::Update()` → `World::Update()`: **the whole client ticks here.** |
| 40412 | `PlayerCharacter::dtor_impl` | `HookVMDestructor` | Named "VM destructor" in our code, but it is the player character's destructor; used as a shutdown signal. |

- `HookVMUpdate` only ticks when the VM is active: `SkyrimVM::inactive` (`int32` at
  **`0x690`** on AE, per upstream TiltedEvolution) must be 0. Our fork used to read a byte at
  `0x680` (SE layout) and ticked unconditionally "so the server accepts connections in menus";
  reverted 2026-10-10 to upstream's behaviour. If hosting from menus is needed, pump only the
  network there, not `World::Update()`.
- Because the tick lives in `SkyrimVM64.cpp`, which nothing references directly, a static-archive
  link silently dropped it (see [plugin linking](plugin-linking.md)): the plugin loaded but never ran.

## Native function registration (`Code/client/Games/Skyrim/Papyrus/VirtualMachine.cpp`)

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 55739 | (unnamed) bind-all-natives | `HookBindEverythingToScript` | **SKSE hooks this too** (`Hooks_Papyrus.cpp: RegisterPapyrusFunctions`). We bind `DidLaunchSkyrimTogether` (upstream's `IsRemotePlayer` / `IsPlayer` went with its patched scripts, 2026-10-20). |
| 104788 | (unnamed) register native | `HookRegisterPapyrusFunction` | Fires `PapyrusFunctionRegisterEvent`. |

- The host's script engine also treats every party member as the player: comparisons, `Game.GetPlayer()`,
  queued calls and stacks ([papyrus-guest-scripts.md](papyrus-guest-scripts.md)).
- Native function objects we hand to the VM are owned and freed by the VM **through the game
  heap**, so they must be allocated with `Memory::New<T>()`, not `new` (fixed 2026-10-10).
- Our `NativeFunction` ctor (ID 104653) and dtor (104655) match SKSE's
  `PapyrusNativeFunctions.h` (`Impl_dtor` = 0x0143E330). Layout: `NativeFunctionBase` is 0x50
  bytes; compare with SKSE's field comments and CommonLib `RE/N/NativeFunctionBase.h`.
- The bound function targets the script `SkyrimTogetherVerifyLaunchScript` from the mod's data files
  (`GameFiles/`). Without it installed the VM cannot attach it.

## Freezing the world: `Main::freezeTime` (2026-11-02, 1.7.104)

`Main` (singleton 403449, `Games/Skyrim/Main.h`) has `freezeTime` at 0x16 and `freezeNextFrame` at 0x17 (CommonLibSSE
`RE::Main`). `Main::Update` (36564, 0x140658870 in 1.7.104) tests `freezeTime` next to the menu-mode flag (UI +352,
`numPausesGame`) at five places, and skips together what a pausing menu skips:

- the world root's update, `Calendar::Update`, `BGSImpactManager`;
- the sky cell's update;
- the player's update (`TES::sub_13288` with the player);
- `ProcessLists` (every actor's process and AI), `Projectile`, `TES::sub_13292`;
- one more call just before the VM update.

`SkyrimVM`'s update still runs, and our update with it (HookVMUpdate), so the network and the harness keep going.
PhotoMode (`reference/plugins/PhotoMode`) toggles the same flag.

The repro harness's `world_freeze` sets it, and `step` clears it for N frames (`TestHarness/FrameStepper.h`). Our
update runs after the frame's world update, so clearing it there lets exactly the next frame run. On the duo: frozen,
both games' world-frame counts stop, game time stays put and the guest stays connected; `step(10)` moves both by
exactly 10. Not yet checked: whether Havok's step is among the skipped work (a body in the air while frozen).
