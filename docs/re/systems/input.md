# Input

How a key press becomes a player action, and how to inject one the way real input arrives
(for test scenarios: the other game must see exactly what a player's cast or swing produces).

## The path (1.6.1170, verified in IDA 2026-10-11)

| Step | ID | What |
|---|---|---|
| `Main::Update` → `BSInputDeviceManager::PollInputDevices` | 68617 | Once a frame. **We hook it** (`Code/client/Games/Skyrim/BSInput/BSInputDeviceManager.cpp`) and skip it entirely while the game window is not in the foreground, so off-screen test games get no device input at all. |
| devices `Poll` (vfunc 2 of each of the 4 devices at `+0x60`) | | Keyboard, mouse, gamepad, virtual keyboard fill `BSInputEventQueue` (singleton 407374). |
| `ControlMap::MapUserEvents` | 68542 | Gives each queued event with an **empty** `userEvent` the user event mapped to its (device, idCode) in the active input contexts; disabled controls get "Skip", then "". Events that already carry a `userEvent` keep it. |
| `BSTEventSource<InputEvent*>::SendEvent` | 68655 | Sends the chain (`BSInputEventQueue+0x380` = head) to every sink of `BSInputDeviceManager` (singleton 402776; the event source is at offset 0). CommonLib inlines this template, so no community ID. |
| `PlayerControls::ProcessEvent_InputEvent` | 42338 | Bails while a menu, VATS, a dialogue etc. is up. For each event: `handler->CanProcess` (vfunc 1), then by type `ProcessButton` (vfunc 4) / `ProcessMouseMove` (3) / `ProcessThumbstick` (2) with `PlayerControlsData*` = `this+0x24`; held handlers also get `UpdateHeldStateActive` (vfunc 5). |
| `AttackBlockHandler::CanProcess` / `ProcessButton` | 42454 / 42421 | Accepts `UserEvents::leftAttack` (+0x40) and `rightAttack` (+0x48); also handles `forceRelease` (+0x58) and `dualAttack` (+0x50). Value 1 with held 0 is the press; value 0 with held > 0 the release. A spell in hand charges on press and is cast on release (through `ActorMagicCaster::SpellCast` 34144, which we hook as `HookSpellCast`). |
| `ReadyWeaponHandler::ProcessButton` | 42428 | Draw/sheathe toggle on press (value != 0, held == 0). |

## Layouts (AE; check CommonLibSSE-NG before trusting powerof3's)

- `InputEvent`: vtable `+0x00`, `device` `+0x08` (keyboard 0, mouse 1, gamepad 2, virtual keyboard 3;
  the engine's own synthetic events use 5), `eventType` `+0x0C` (button 0, mouse move 1, char 2,
  thumbstick 3, device connect 4), `next` `+0x10`.
- `IDEvent`: `userEvent` (`BSFixedString`) `+0x18`, `idCode` `+0x20`.
- `ButtonEvent`: `value` `+0x28`, `heldDownSecs` `+0x2C`; **size 0x30**. vtable ID 208708.
  CommonLibSSE-NG PR #347: code that assumed `sizeof(ButtonEvent) == 0x18` overwrote fields and
  crashed in `BSStringPool::Entry::release8`.
- `UserEvents` singleton 402638: `BSFixedString` members from `+0x08` (`forward`); see
  `reference/CommonLibSSE/include/RE/U/UserEvents.h`. The strings are interned, so they compare by pointer.
- `PlayerControls` singleton 400864: `PlayerControlsData` at **`+0x24`** (0x20 on SE), handlers
  `BSTArray` at `+0x58`, `readyWeaponHandler` `+0x188`, `attackBlockHandler` `+0x1B8`.

## Injecting input

What mods do (no hook needed): build a `ButtonEvent` and send it through `BSInputDeviceManager`'s
event source, so every sink sees it exactly as it would see a device event.

- [DualCastHotkey](https://github.com/Shikis01/DualCastHotkey) (`plugin.cpp`): press = `ButtonEvent::Create(kMouse,
  ue->rightAttack, button, 1.0f, 0.0f)` → `BSInputDeviceManager::SendEvent`; release = the same with
  value 0 and the measured held time. It sends no per-frame "still held" events; the handler's held
  state covers the hold.
- [KeyLogicExpansion](https://github.com/Acook1e/KeyLogicExpansion) (`src/lib/hook.cpp`): hooks
  `AttackBlockHandler::ProcessButton` and rewrites `userEvent` to `rightAttack` before calling the
  original.

Ours: `TestHarness::EngineCalls::SendKey` (the harness's `key` command) maps the key first, as the poll
does: `ControlMap::MapUserEvents` (68542, `(ControlMap* 400863, InputEvent* chainHead)`; verified in IDA
2026-10-13) with an empty `userEvent`, so menus get their own events (the map's P is
`PlacePlayerMarker`). Then `SendEvent`. Its `mouse` command does the same with a mouse button or a
`MouseMoveEvent` (vtable 236703, idCode 0xA "Cursor", deltas `mouseInputX/Y` at `+0x28/+0x2C`), which moves
the menu cursor (`MenuCursor` 403551, `cursorPosX/Y` at `+0x4/+0x8`, window pixels).
`TestHarness::EngineCalls::SendPlayerButton` (`Code/client/TestHarness/EngineCalls.cpp`; the harness's `input` command, test builds only; `scripts/coop_test.py` press/release/tap).
What it took to drive a player like this (verified 2026-10-11):

- **A held button is sent every frame**, value 1 with the time held so far, as devices do (`TestHarness::PlayerInput::Poll`).
  Spells charge without it (the magic caster drives the charge), but a melee swing only starts from
  those updates: a press and release with nothing between does nothing.
- **Presses during an animation are ignored**, a player's too. Wait for the game, not for time:
  - draw/sheathe: graph `IsEquipping` / `IsUnequipping`. A spell reads "drawn" (weapon state 3) on the
    press itself while its animation still plays, and the flag only rises a frame later, so the
    harness waits for the plugin's per-frame count of these animations to go up, then for them to end.
    Equipping a new item with the hands up plays one too (`MagicForceEquipRight`).
  - casting again: caster state back to 0 **and** graph `IsCastingRight` false (the release
    animation, about a second).
- `MagicCaster::State` (right hand, Firebolt): 2 while charging, 3 charged (release now), 4 just
  after the release, 0.
- **A move with `SetPosition` leaves the character controller in the air** for a few frames
  (`hkpCharacterContext::currentState`, `bhkCharacterController+0x1E0+0x20`: 0 ground, 2 air). A
  draw or charge started then is cut short on landing (`MRh_SpellReady_Event` refused, then
  `SpellCast(false)` and an interrupt). The controller notices the move a frame later.

Notes for us:
- Call `SendEvent` (68655) on the main thread, outside `PollInputDevices`. It is not gated by our
  foreground check, so it works for off-screen test games.
- Allocate the event ourselves (0x30 bytes, vtable 208708). Copy `UserEvents` strings by pointer only
  if the event is never destroyed through its vtable (the destructor releases `userEvent`).
- `PlayerControls` ignores input while menus are open, the same as for a real player.
- `ActorMediator::PerformAction` (with `TESActionData`) skips all of this. It starts the animation, but
  no cast (`SpellCast` never runs) and, from our client, nothing our animation sync sees (it calls the
  original past our hook on 38949).

## Taking input (the chat line)

We hook `BSTEventSource<InputEvent*>::SendEvent` (68655; verified in IDA 2026-10-13: `(source, InputEvent* const*
chain)`, each sink's `ProcessEvent` is vfunc 1 with `(sink, chain, source)`; its one caller is
`PollInputDevices`). `UiInput::Take` (`Code/client/Ui/UiInput.cpp`) sees every chain first:

- Chat closed: an Enter press (scan code 0x1C, or 0x9C on the numpad) while only the HUD is up opens it;
  nothing else is touched.
- Chat open: it keeps the keyboard presses and the chain goes nowhere, so the game gets no input at all
  (no walking while typing, Escape opens no menu). A menu opening or the session ending closes it.
- Text: scan code → virtual key → `ToUnicodeEx` with the thread's keyboard layout, shift from the events
  and Caps Lock from Windows, as skymp's `InputConverter` does (`reference/plugins/skymp`). No dead keys
  or IME.

The harness's injected keys (`SendKey`, `{"cmd": "key"}`) call the same function, so they go through the
hook like a keyboard's: `chat-reaches-everyone` types with them.
