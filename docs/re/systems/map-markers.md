# Map markers

The marker a player places on the world map, and how a player places one. Verified in IDA on 1.6.1170
(2026-10-13).

## Placing and removing

| Step | ID | What |
|---|---|---|
| `MapMenu` gets `PlacePlayerMarker` (P, `controlmap.txt` Map Menu context) | 53102 | Its message handler, `kUserEvent`: P calls `PlaceMarker`; Cancel (Tab/Escape) or Quick Map (M) close the map. Ignored while fast travel is confirmed (`+0x3058E`). |
| `MapMenu::PlaceMarker` | 53113 | Needs `controlsReady` (`+0x3058C`, CommonLibSSE-NG `RUNTIME_DATA2+0x12C`), no pick pending (`+0x30580`) and the local map closed (`+0x3045C`). No marker yet: starts the pick (53109). A marker already: the "Do you want to move your marker or remove it?" box with Move It (0) / Leave It (1) / Remove It (2), answered by `PlacePlayerMarkerCallbackFunctor::Run` (53128). |
| `MapMenu` pick | 53109, 53114 | The ray through the menu cursor (`NiCamera::WindowPointToRay` 70630) against the LOD land node, five frames later; a hit calls 40535, a miss plays the fail sound. |
| `PlayerCharacter::SetPlayerMapMarker` | 40535 | `(player, NiPoint3* position, TESForm* worldOrCell)`. Creates the marker reference once (handle at `PlayerCharacter+0x92C`), moves it there, parents it to the worldspace's persistent cell (or the interior cell). Reads the form's type **without a null check**. Its one caller is the pick (53114). **We hook it** (`HookSetWaypoint`). |
| `PlayerCharacter::ClearPlayerMapMarker` | 40536 | Releases the reference and sets the handle invalid. Callers: the box's Remove It (53128, map open), the player's destructor, and an engine reset (`sub_1401B9AB0`, from `TES` and `Main`). **We hook it** (`HookRemoveWaypoint`) and send only while `MapMenu` is open, so a reset never clears everyone's marker. |

The bridge places a received marker through the hooks' originals (the trampolines), so applying one
is never sent back.

## Opening the map

- M (`Quick Map`) opens `MapMenu` with a fade (`Fader Menu`); `controlsReady` comes up with it.
- The map camera's arrival callback (`IMapCameraCallbacks` 53119) opens the `Cursor Menu` and shows the
  map's tutorial once (`sub_14096FE40(250)`: default object 250, recorded in `TutorialMenu`'s shown list
  406588). It shows only while `bShowTutorials:Interface` is on, a **Skyrim.ini** setting
  (`gIni_bShowTutorials_Interface`); SkyrimPrefs.ini ignores it. The tutorial is a modal menu and takes
  the next key.

## The "move or remove" box

- Vanilla PC message boxes take only the mouse: no key moves between buttons or picks one (keyboard
  navigation is what the "Better MessageBox Controls" mod adds), and a click must land on the button's
  text. The box is always-open, so until it is answered the game takes no other input.
- Our `UI.cpp` clears `kPausesGame` on `MessageBoxMenu` while connected (its unpaused-menu list); keeping
  it paused over the paused map made no difference, so that is not why keys do nothing.

## Test games

- `map-marker-reaches-everyone` drives the box as a mouse player: the harness's `mouse` command sends a
  `MouseMoveEvent` (vtable 236703, idCode 0xA, deltas at `+0x28`) and mouse buttons through the control
  map and the input dispatch; the menu cursor (`MenuCursor` 403551, window pixels, about 0.98 px per unit)
  is in the status. "Remove It" is at (746, 393) and "Leave It" at (630, 393) at the test games' 1280x720.
- Keys from the harness go through `ControlMap::MapUserEvents` (68542) first, so P reaches the map as
  `PlacePlayerMarker` (`docs/re/systems/input.md`).
- A game's first map shows the map tutorial, which takes the next key; test games turn tutorials off in
  Skyrim.ini.
