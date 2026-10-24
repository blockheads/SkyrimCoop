# In-game UI and native workbench

RmlUi 6.2 draws the documents in `assets/`. The game uses `UiRenderer` (D3D11); `UiWorkbench`
loads the same files on Linux with fixtures and the stock GLFW/GL3 backend. No Skyrim is needed.

| Location | Responsibility |
|---|---|
| `Code/ui_models` / `UiModels` | Engine-free models, shared player colors, party activity/fade policy and RmlUi bindings |
| `UiService` | Game service reads, party order/health, chat history, expiry, keyboard input and commands |
| `assets/join.rml`, `join.rcss` | Monochrome connection box, loading bar and refusal/end details |
| `assets/chat.rml`, `chat.rcss` | Compact transparent chat: AOL-style text and individually colored names |
| `assets/party.rml`, `party.rcss` | RCT-style health faces next to player names and levels |
| `assets/moods` | Seven downloaded guest mood PNGs, a new dead skull, sources and generation prompt |
| `assets/aol-text.rcss` | 17dp chat typography and readable shadow |
| `assets/fonts` | Oswald and Liberation Sans, with their licenses |
| `Code/ui_workbench` | Fixtures, native input, asset reload, PNG loading, screenshot/check modes |

## Running

```bash
scripts/check-ui.sh             # build + smoke checks at 720p, 1080p, 1440p
scripts/check-ui.sh --no-build  # assets-only iteration
scripts/check-ui.sh --shots     # also write review PNGs to build/ui-previews
scripts/xmake-linux.sh build -j8 UiWorkbench
build/linux/x86_64/debug/UiWorkbench
build/linux/x86_64/debug/UiWorkbench --fixture chat-open
build/linux/x86_64/debug/UiWorkbench --fixture party-mood-dead
build/linux/x86_64/debug/UiWorkbench --shot join-receiving-world build/connecting.png
build/linux/x86_64/debug/UiWorkbench --shot chat-open build/chat.png --size 1280x720
build/linux/x86_64/debug/UiWorkbench --check
```

F6/F7 select the next/previous JSON fixture; F5 reloads; F9 cycles 1080p, 720p and 1440p.
The title names the selected fixture. Changes under `assets/` reload within one second and restore
that fixture's data. `SKYRIMCOOP_UI_DIR` overrides the assets path; otherwise the binary uses the
checkout recorded at build time. Both wrappers select this checkout explicitly for worktree builds.

Sizes are in dp, with density `framebuffer height / 1080` in both the game and workbench.
Screenshot/check modes start a private Xvfb display, use software GL and freeze animation time;
install Xvfb and a Mesa OpenGL runtime. They do not need a desktop. `--shot` accepts a fixture name,
JSON filename or path. Its output parent must exist. Missing assets, malformed fixtures, render
errors and PNG write failures return nonzero. RmlUi warnings also fail `--check`.

The gate (`scripts/check.sh`) builds the workbench and checks every document with every fixture.
It checks Copy/Back, document reload, removal of manual connection controls and live health-to-image
binding at every threshold, including zero and unavailable health. The eight `party-mood-*` fixtures
show the complete health scale. Other fixtures cover party sizes, low/absent health, open/closed chat,
connection steps and every refusal/end reason.
Presentation checks also cover distinct/stable colors, matching party/chat colors after roster changes,
activity expiry/fading, health jitter, death/revival, native-HUD suppression, queued activity and disconnect.
`party-hidden`, `party-fading` and `party-last-left` are static inspection fixtures. Other party fixtures
stay visible for inspection; the game runs the activity clock, and `--check` tests it with synthetic time.

## Presentation

The connection overlay is a simple black box with a thin white border and the same 17dp Liberation
Sans text already used by chat, with bold error headings. It uses the existing bundled fonts.
Hyphen-prefixed details and bracketed Copy/Back buttons give it an ASCII-like presentation.
There is no title bar, shell prompt, command text or cursor. An activity bar shows unknown progress,
and a measured bar/percentage shows known progress. Refusals/end states show a bold heading and
details; Copy retains the reason code. Buttons invert to black-on-white on hover/focus. Only the
box has a backdrop; the game outside it stays visible.
Idle says “Waiting for a Steam invite.” There is no address form, marketing copy, step rail or
manual Join/Host action in this panel. The UI follows state supplied by the session; it performs no
Steam or network work. Character creation is owned by the game: the old creation fixture is removed
and the overlay is suppressed for the compatibility `creating_character` state.

Chat uses a 500dp-wide transparent text area, with no title bar, beveled frame, message background,
Send visual or extra hints. Names are bold and individually colored; normal text
is light and shadowed so it stays readable over the game. `aol-text.rcss` defines chat's typography.
Chat uses 1.1 line height with no extra line gap and a 4dp entry gap to keep it short.
Chat keeps the existing behavior: six recent lines while closed, twelve while open, Enter to send,
Escape to cancel, and the game's keyboard-layout typing/backspace/commands. `OnlineHost:` labels
system lines as in AOL. Sending remains keyboard-driven.
The preview contains selected lines; history and expiry remain in UiService. Member and chat-line
bindings retain their previous names, so existing fixtures and game producers still work.

## Player colors and party visibility

One `PlayerColors` registry supplies both party rows and chat lines. Each display name keeps its color
through message expiry, roster reordering, departures/rejoins and document reload. A bright palette
avoids duplicate assignments; longer sessions generate additional light colors. `OnlineHost` stays
neutral gray. The protocol identifies chat authors by display name, so identical display names share
a color. Levels remain gold, and unavailable characters remain dimmed.

The party panel shows for eight seconds after someone joins/leaves, dies/revives, becomes available,
or their health changes, then fades out over 400ms. A 0.1%-of-max-health threshold accumulates changes
against the previous significant sample to ignore float jitter; death/revival always trigger, even
below that threshold. Steady low health, level updates and row reordering do not keep the panel awake.
Who joins, leaves or goes down is a chat line, not a panel notice (below). Disconnect clears activity.

`UiInput::IsHudVisible` reads the existing engine menu-system flag, HUD menu and blocking menus, then
the native HUD's root/health-clip visibility. `HUDMenu.ShowElements` controls the health clip's
`_visible` from HUD modes; its normal full-health fade happens inside the clip. This follows native
menu/cinematic/hidden-HUD suppression while another player's health can still wake the party panel.
See the [HUD mode implementation](https://github.com/Mardoxx/skyrimui/blob/master/src/hudmenu/HUDMenu.as).
A replacement HUD missing those clips falls back to engine/menu visibility. Activity while the HUD
is hidden is shown when it returns; merely opening a menu does not wake an inactive panel.

`PartyModel.Visible` and `Opacity` bind as `visible` and `opacity`. Game harness
state keeps the full `party_panel` data while hidden and reports `party_panel_visible` and
`party_panel_opacity` separately. Party/chat rows expose `color` for live checks. The native timing
policy and bindings are tested in the workbench; suppression through Skyrim's actual HUD still needs
in-game validation after integration.

## Health faces

`Member.Health` is still the game's normalized health, with -1 for an absent character. Its derived
`face` binding chooses a PNG; no new game service state is needed. Names and levels remain text.
Levels are bright gold (`#ffd700`); Loading stays gray.

| Health | Face |
|---|---|
| 90–100% | Very Happy |
| 75–<90% | Really Happy |
| 60–<75% | Happy |
| 45–<60% | Neutral |
| 30–<45% | Sad |
| 15–<30% | Tired |
| >0–<15% | Very Tired |
| 0% | Dead skull |
| unavailable | Dim neutral face, “Loading” |

This is a health scale, not a separate mood/tiredness statistic. The seven downloaded PNGs are
unchanged; see [their provenance](assets/moods/README.md). The skull is a new transparent imagegen
asset with a bare cranium, hollow sockets and a distinct toothy jaw. It draws at 48 by 50dp, centered
in the living faces' 54 by 50dp space, with the name column kept aligned. Its exact prompt is
[saved beside it](assets/moods/dead-prompt.txt). No health bar is drawn.

## Join presentation contract

`Ui::JoinModel` binds the `join` model. Keep model objects in stable storage while bound documents
use them. Bind once per context; dirty changed fields using `Handle`, or `Handle.DirtyAllVariables()`.
Document reload does not recreate or rebind the models. The library has no engine, World, service or
D3D includes.

| Field / binding | Meaning |
|---|---|
| `Visible` / `visible` | Show the connection box; default false |
| `State` / `state` | idle, connecting, checking, receiving_world, creating_character, loading, joined, hosting, refused, ended |
| `Step` / `step` | Current stage 0..6; retained as state data, no numbered stage list is drawn |
| `Completed` / `completed` | Count of completed stages 0..6; retained for the session producer |
| `Progress` / `progress` | Measured transfer fraction 0..1; -1 uses an activity bar, with no invented percentage |
| `Address` / `address` | Retained for source compatibility; no manual address input is drawn |
| `Reason` / `reason` | Stable refusal/end code supplied by the session and included in copied details |
| `Message` / `message` | Current status sentence; refusal wording comes from the existing JoinRefusal producer |
| `Details` / `details` | Ordered plain-text error/difference lines |
| `Handle` | RmlUi dirty notifications |

The join preview handles the Copy and Back example actions. Copy includes the stable reason,
message and every detail. Back restores the waiting-for-invite preview. It does not run a session.
Existing stage/refusal fixtures remain useful for exercising status text and loading/error states.

## Preview limits and game hookup

WorkbenchRenderer uses the same default no-ops as the game's renderer for clip masks, layers,
filters, saved layers/masks and box-shadow, and accepts the same linear/radial/conic gradient families
(up to 16 stops). Avoid styling with those omitted features.

The workbench now loads PNGs through stb_image with premultiplied RGBA, so the same mood PNGs work
in the game through WIC. The stock loader remains available for uncompressed 24/32-bit TGA. JPEG,
BMP and other WIC codecs are still game-only. GL3/D3D11 rasterization, texture filtering and FreeType
platform hinting can differ slightly. Workbench MSAA is off; Skyrim owns its render target. The
preview has a flat background; Skyrim supplies its actual world/menu background. Screenshot time
is frozen; normal interactive and game clocks run normally. Fonts and image files reload as assets.

The game loads the party, chat and join documents.

## In the game

- **The join panel** follows `JoinService::GetStatus()` (`UiService::UpdateJoin`): shown while a join is on
  its way (connecting, checking, receiving the world with its fraction, loading), while a guest that lost
  its host reconnects, on a refusal and when a session ended (lost after its tries, or kicked); hidden
  when joined or idle. A refusal brings its sentence and details (`JoinRefusal`); the steps on the way are
  worded by the panel. Back hides the refusal until the status changes; Copy puts `JoinModel::CopyText`
  (shared with the workbench) on the Windows clipboard.
- **Input:** while the refusal's buttons show, `UiInput` takes the left mouse button and Escape from the
  game (the main menu gets neither); the menus' cursor (`MenuCursor`) moves RmlUi's pointer. Escape is
  Back, for a refusal over gameplay, where no cursor is up.
- **Chat lines about the session** (`SessionNoticeEvent`, worded by `SessionService`): "Lydia joined the
  party." for a player arriving (`NotifyPlayerJoined::Arrived`; not for the players a newcomer is told
  about), "Lydia left the party.", and "Lydia has perished." in red (`ChatLine::Perished`) on every game
  when a player goes down (`PlayerPerishedRequest`, relayed by `HostSession` as `NotifyPlayerPerished`).
- **Checks:** GameTests "A player joining, going down and leaving"; HostAuthorityTests (arrived, perished);
  scenario `join-panel-shows-refusal` (harness `ui_element`, `clipboard`).

## V1 menu and character flow

Skyrim owns its normal main menu and save/load screens. There are no added Host Game or Characters
entries, custom save picker, character library/default-profile selector, standalone character export,
or disposable character-creation world. Hosting setup stays with the session/Steam work.

Steam joining owns entry into the co-op flow. A player's existing character in the host's world is
preferred. Making a new character uses Skyrim's normal New Game flow; the join service can bring the
character from that loaded game. Campaign character records remain authoritative for returning
players. The UI supplies connection/loading/refusal presentation and yields to Skyrim whenever its
native character creator is active. It does not introduce another character-management route.

The compatibility `creating_character` state keeps the join overlay hidden; there is no character creator
of ours.

`scripts/check-ui.sh` builds the native workbench and checks every party/chat/join fixture at 720p,
1080p and 1440p. It uses private Xvfb displays and never starts, deploys, connects to, or stops Skyrim.
Checks cover asset loading, RmlUi warnings, Copy/Back and reload, health-face bindings, stable player
colors, party activity/fade timing, and native HUD visibility policy. Logs are in `build/check-ui`;
`--no-build` skips compilation and `--shots` writes review PNGs to `build/ui-previews`.

The normal `scripts/check.sh` also runs the workbench checks at its default viewport and builds the
Windows plugin. Native HUD/renderer behavior and the session's Steam join/character flow still need
Skyrim validation when game testing is available.
