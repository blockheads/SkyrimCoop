# In-game UI (RmlUi)

The UI is [RmlUi](https://github.com/mikke89/RmlUi) 6.2: documents in an HTML-like markup (`.rml`)
styled with CSS (`.rcss`), drawn into the game's frame by our own Direct3D 11 renderer. It replaces
the old CEF overlay (`Code/skyrim_ui`), which never built for the clang-cl client.

| File | What it is |
|---|---|
| `UiService` | Starts RmlUi on the game's D3D11 device, loads the documents, keeps their data models current, reloads them when a file changes |
| `UiRenderer` | RmlUi's `RenderInterface` for D3D11: geometry, textures (WIC), scissor, transforms, gradient decorators. Saves and restores the game's pipeline state around each frame |
| `assets/skyrim.rcss` | The shared look: Oswald (a free Futura Condensed lookalike, OFL), off-white text with a soft shadow, rules that fade out at their ends, dark panels that fade to the right |
| `assets/party.rml`, `party.rcss` | The party panel, after SkyUI's HUD widgets: everyone else in the session while connected, a name over a thin health bar that empties toward its centre |
| `assets/chat.rml`, `chat.rcss` | The chat at the bottom left: the last lines for 10 s after they arrive; Enter opens its line (more lines, a dark backdrop), Enter sends, Escape cancels |
| `UiInput` | The keyboard for the chat line: the input dispatch hook (`BSInputDeviceManager.cpp`, 68655) gives it the key presses while the line is open, and the game gets no input then; `UiService` turns them into text with the player's keyboard layout |

## Working on it

- **Edit and look.** The test games (`scripts/coop_test.py`) read the UI from this folder through
  Wine's `Z:` drive (`SKYRIMCOOP_UI_DIR`), and `UiService` reloads every document within a second of
  a change: save a `.rml` or `.rcss` and look at the game. No build.
- **Shipped games** read `Data/SKSE/Plugins/SkyrimCoop/ui`, which `coop_test.py` deploys with the DLL.
- **Sizes** are in `dp`: one dp is one pixel at 1080 lines, scaled to the game's resolution.
- **RmlUi has no default style sheet**: an element is inline until it gets a `display`
  (`skyrim.rcss` sets the common ones).
- **Data** comes from C++ data models (`party` in `UiService::Create`): `{{ member.name }}`,
  `data-for`, `data-if`, `data-class-*`, `data-style-*`. A new value shows once the model's variable is
  marked dirty.
- **Errors** (a bad property, a missing file) go to `tp_client.log` as `RmlUi: ...`.

Not drawn yet: clip masks, layers and filters (`filter`, `backdrop-filter`, `box-shadow`); RmlUi
skips them. Only the chat line takes input (keys, no mouse); the documents never get RmlUi input events.
- **Absolute positions from the bottom** need a body the size of the screen (`chat.rcss`): a body is only
  as tall as its content otherwise.
