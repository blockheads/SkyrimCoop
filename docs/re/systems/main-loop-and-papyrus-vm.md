# Main loop and Papyrus VM

How the client gets a per-frame tick, and what it does inside the Papyrus VM.

## Hooks (`Code/plugin/SkyrimVM64.cpp`)

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
- `HookMainLoop`'s tasks and `World::Update` do not overlap: the job with the update is released after the tasks drain and has ended before the next frame (lock-order.md, "Orders the frame keeps apart").
- Because the tick lives in `SkyrimVM64.cpp`, which nothing references directly, a static-archive
  link silently dropped it (see [plugin linking](plugin-linking.md)): the plugin loaded but never ran.

## Native function registration (`Code/engine/Papyrus/VirtualMachine.cpp`)

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 104788 | (unnamed) register native | `HookRegisterPapyrusFunction` | Adds it to `PapyrusNatives` (locked: the VM registers off the main thread too). |

- The host's script engine also treats every party member as the player: comparisons, `Game.GetPlayer()`,
  queued calls and stacks ([papyrus-guest-scripts.md](papyrus-guest-scripts.md)).
- Native function objects we hand to the VM are owned and freed by the VM **through the game
  heap**, so they must be allocated with `Memory::New<T>()`, not `new` (fixed 2026-10-10).
- Our `NativeFunction` ctor (ID 104653) and dtor (104655) match SKSE's
  `PapyrusNativeFunctions.h` (`Impl_dtor` = 0x0143E330). Layout: `NativeFunctionBase` is 0x50
  bytes; compare with SKSE's field comments and CommonLib `RE/N/NativeFunctionBase.h`.
- The bound function targets upstream's script `SkyrimTogetherVerifyLaunchScript`, no longer bundled (it went with
  `SkyrimTogether.esp`, 2026-11-26): nothing calls it.

## Hooks off the main thread (2026-11-27, 1.7.104)

Where our hooks run besides the main thread and our update (the check in `World::Get()`, test and debug builds; since
2026-11-27 none of them reaches `World::Get()`):

- `UI::ProcessMessages` (82082, `UI_FindOnStack_Hook`): also from `Job_UI` (a job thread) and from the loading
  screen's loop (`0x1406E4C20`), which runs on its own thread while the main thread loads.
- Present (77246, `Hook_StopTimer`, so `RenderSystemD3D11::OnRender`): from `Main` each frame, and from that loading
  screen loop. The loop also polls input (`BSInputDeviceManager::PollInputDevices`, 68617, so `UiInput::Take`), so the
  UI's input and its drawing always share a thread. `UiService` draws from a snapshot the main thread fills each
  update (one mutex); a sent chat line or Back goes to the main thread through `World::Runner()`. During a load the
  snapshot is the last one before it (the join panel shows "Loading the world..." with its activity bar).
- Papyrus natives and their registration (104788), graph updates and events (63587, 58329, 63568, 63591), actor
  animation jobs (41453), `MagicTarget::AddTarget` (34526), a melee hit's damage and stagger (`HookDamageActor`, 37710),
  `BSTimer::SetGlobalTimeMultiplier` (68245, on a guest's death, a new thread each time): the engine's job and VM threads.

Those hooks read what is safe there through `World`'s static accessors (`Hosts`, `Connected`, `FrameTick`, `Streams`,
`Created`; `World.h`) and queue the rest through `World::Runner()`.

## Freezing the world: `Main::freezeTime` (2026-11-02, 1.7.104)

`Main` (singleton 403449, `engine/Main.h`) has `freezeTime` at 0x16 and `freezeNextFrame` at 0x17 (CommonLibSSE
`RE::Main`). `Main::Update` (36564, 0x140658870 in 1.7.104) tests `freezeTime` next to the menu-mode flag (UI +352,
`numPausesGame`) at five places, and skips together what a pausing menu skips:

- the world root's update, `Calendar::Update`, `BGSImpactManager`;
- the sky cell's update;
- the player's update (`TES::sub_13288` with the player);
- `ProcessLists` (every actor's process and AI), `Projectile`, `TES::sub_13292`;
- one more call just before the VM update.

`SkyrimVM`'s update still runs, and our update with it (HookVMUpdate), so the network and the harness keep going.
PhotoMode (`reference/plugins/PhotoMode`) toggles the same flag.

The repro harness's `world_freeze` sets it, and `step` clears it for N frames (`harness/TestHarness/FrameStepper.h`). Our
update runs after the frame's world update, so clearing it there lets exactly the next frame run. On the duo: frozen,
both games' world-frame counts stop, game time stays put and the guest stays connected; `step(10)` moves both by
exactly 10. Not yet checked: whether Havok's step is among the skipped work (a body in the air while frozen).
