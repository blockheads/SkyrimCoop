# Message boxes

How the engine shows a message box (`MessageBoxMenu`), and the Anniversary Edition box that a fresh test prefix
brought into every duo boot. Read in IDA on 1.7.104 (2026-11-07) unless a line says measured or inferred.

## The queue and the menu

| Step | ID | What |
|---|---|---|
| `MessageBoxMenu::CreateMessage` | 52269 | `(text, callback, ?, warningType, menuDepth, buttons..., nullptr)`: a new `MessageBoxData` (0x50) to `QueueMessage`. Also wraps the old function-pointer callbacks. |
| `MessageBoxMenu::QueueMessage` | 52271 | Refuses while `TES+0x137/0x138` (loading), when 20 are queued (type 25 always passes), or for a suppressed type (0-30). Turns `\r` in the body into spaces, inserts the box sorted into the queue (406362, lock 406365), and sends `kShow` unless `Loading Menu` is open. |
| `MessageBoxMenu::ProcessMessage` | 52280 | Vtable 215795 slot 4. `kShow`: shows the next box unless the queue is empty or `Loading Menu` is open. `kUpdate` with a queue: the next box. |
| `MessageBoxMenu::ShowNextMessage` | 52285 | Drops suppressed types, then shows the newest box: `setMessageText(bodyText +0x10, useHtml +0x4D)`, `setIsVertical(+0x4E)`, `setIsCancellable(+0x4F, cancelButtonIndex +0x3C)`, `setButtons(gamepad, buttonText +0x20...)`. Queue empty and nothing shown: `kHide`. |

`MessageBox.as` (reference/plugins/skyrimui) sizes the frame from the text and the buttons: with an empty body and no
button drawn it is 60 px wide and about 90 tall, the narrow empty frame in the bug's screenshot.

## The Anniversary Edition box

- `MainMenu::ProcessMessage` (52137, vtable 215698 slot 4), `MainMenu::OnLogoShown` (the logo's first show) and the
  save-list path all call `MainMenu::QueueFreebiesPrompt` (52156) while `bFreebiesSeen:General` (SkyrimPrefs.ini) is
  0 and not every free Creation is installed (35477).
- 52156 sets the pref to 1, asks the prefs collection to save, and queues the box: body `$Thanks for buying Skyrim
  Anniversary Edition! Download and activate?`, one button `$DOWNLOAD`, type 25 (always queued), menu depth 10, and
  the `DLCPurchased::PurchasedDLCDialogCallback`.
- The body's key is missing from `interface\translate_english.txt` (Skyrim - Interface.bsa; `$DOWNLOAD` is there), so
  the body shows empty. Read from the BSA.
- Measured: the pref never reached SkyrimPrefs.ini in a game the harness killed, so the box came back on every boot
  of a fresh prefix until one game ended another way.

## The test games

- A fresh worktree's prefix took `Low.ini` as SkyrimPrefs.ini, with no `bFreebiesSeen`. The harness's boot command
  runs `coc` at the main menu, so the box queued there opened over QASmoke outside a session, and paused the game.
- Measured (2026-11-07, 13 of 33 runs that day, none on 2026-11-01 to 04): `MessageBoxMenu`, `Cursor Menu` and
  `Fader Menu` open in both games; the plugin's heartbeat stopped about 2 s after `coc`. `TestHarness::Poll` runs in
  our main-loop hook every frame, so the main thread itself stopped, and the host's "hosting" step timed out. Why it
  stops is not read; the screenshot is the off-screen display's last frame (the `ffplay` window).
- `prepare_prefix` now sets `[General] bFreebiesSeen=1` in SkyrimPrefs.ini. Measured on the same fresh prefix: two
  boots without it showed the box and failed to host; the boot with it hosted, joined and showed no box.
- A player's game shows the box at the main menu, where the player answers it; nothing of ours changes that.
