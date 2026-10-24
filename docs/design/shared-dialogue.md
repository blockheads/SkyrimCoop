# Shared dialogue

**Status (2026-10-22):** built, the host's list for everyone ("Decided 2026-10-22" below): `HostDialogue`, the
client side split by role (`DialogueService`, `HostConversation`, `GuestDialogue`, `DialogueBridge`), the harness
and the scenarios. Step sync (each guest's engine building its own list) was built the night before and replaced.

One conversation for the whole party (`party-model.md`, "Dialogue"). Engine findings and the in-game
experiments it rests on: `docs/re/systems/dialogue.md`. Agreed 2026-10-14, the join rules changed 2026-10-18,
the host's list for every menu 2026-10-22; built: the messages and `HostDialogue`, the host's background
conversation, joining by talking, leaving, picks from anyone, picks in the chat, a picked line's script run for
its picker (its items, gold, Speech experience and training the picker's), refused starts (Build order).

## What players see

- **One shared conversation, always run on the host's game:** its NPC says every line, and its quests and
  scripts run there.
- **A peer talks to an NPC** (E on its copy): the host's game starts the conversation in the background (no
  menu, its player not turned; the host keeps playing and reads the NPC's lines on its HUD, at any distance).
  The peer's menu opens on its copy.
- **The host talks to an NPC:** a normal conversation with its menu. Peers are not pulled in; they read the
  NPC's lines on their HUD.
- **Joining is by talking, never by distance** (decided 2026-10-18, replacing joining within 500 units, which
  itself replaced the gathering of 2026-10-14). The only way into a conversation under way is to go up to the
  NPC (the copy, on a peer) and press E: the menu opens on the conversation as it stands. The host joins its
  own background conversation the same way. Nobody is moved for a conversation, and first and third person
  stay each player's own.
- **Every menu shows the host's list** (decided 2026-10-22, replacing each player's own options): the options the
  host's engine built for the step, with its lines, judged for the host's player (its Speech, gold, race, quests).
  Brenuin's "Here. Have a gold piece." is on every menu when the host carries gold, on none when only a guest
  does. Each player moves their own highlight.
- **Anyone picks, and speaks for the party:** the host's game says that option as if its player had picked it
  (its line, its script, the host's gold for a room). The other menus keep their list until the next one, and
  everyone's chat names the pick: `Lydia: Here. Have a gold piece.` Then every menu shows the host's next list.
- **One conversation at a time.** Talking to another NPC while one is open does nothing (a chat line says
  someone is talking). An NPC's force greet waits too.
- **An NPC that starts a conversation greets the party** (decided 2026-10-19): NPCs run their AI on the host's
  game, so its force greet reaches the host's player, and every player's menu opens on it (the peers' on their
  copies), as for the peer who asks.
- **A question's answers are the host's:** when the host's player has none, the conversation ends for everyone,
  as it does in the game.
- **A start the host's game cannot make is refused** with a chat line: the NPC fighting or dead (as talking
  to them is in the game), the host's player talking to someone else. The host in a menu, in combat or far
  away does not stop it (its world paused pauses the lines).
- **Anyone leaves on their own** (Tab, Esc). The conversation ends only when the last player in it leaves,
  or when the host's engine ends it (a goodbye line, the NPC walking off, combat, travel): then every menu
  in it closes. Walking off does not end it (the engine has no distance rule); the party travelling does, and
  an NPC that goes away (deleted, or left behind) ends it on every game.
- **A picked line's script runs for its picker** (2026-10-22, `party-model.md` rule 2): the host's game says
  it, and `Game.GetPlayer()` in it is the picker's character, so the gold and items it gives or takes (a reward,
  a beggar's coin, a room's rent) are the picker's. The natives that act on the player unnamed run on the
  picker's game: a persuade's Speech experience (`Game.AdvanceSkill`) is the picker's, so a party can have its
  talker, and a trainer's line (`Game.ShowTrainingMenu`) opens the training menu there, over its dialogue menu
  as in single player, with the picker's gold and skills. This replaced two reroutes of their own (Speech
  experience and training, 2026-10-19).
- **Barter, gifts, a follower's inventory** still open on the host's game, where the line is said. With each
  player's own items (`party-model.md`, 2026-10-22) they belong on the picker's game, like training; the NPC's
  side (a merchant's stock and gold, a follower's inventory) is the host's, so its changes go to the host.
- **A line said is said for everyone:** the host's game says every line, so a say-once line (Hulda on the
  College) is not offered again and one with reset hours (a beggar's coin: 4 hours) not until they pass, on every
  menu, since every menu shows the host's list.

## The rule

**The host's game judges and acts** (decided 2026-10-22). Its engine builds each list with the real quests,
aliases and NPC, for its player, and says each pick with its own line and script. A guest's menu is a screen:
it shows the host's options and sends picks. This replaced "judge where the player is, act where the world is"
(2026-10-19), where each game's engine judged its own player's lines: a guest's engine judged quest topics
against quest state it does not run (aliases, running quests), and a pick could carry a line the host's menu
never offered. The cost: options are the host's player's (a Speech check uses the host's Speech). Fitting the
party model (the party is the Dragonborn: quests and identity are shared); a skill override for the picker
is a later piece if wanted. Items and gold are each player's own, so an item or gold condition asks the host's
inventory too (Lucan's claw in a guest's bag offers no hand-in).

**Only the host's game runs the conversation; every other game is a screen** (decided 2026-10-19). A guest's
engine never picks, says a line or runs a fragment.

## Flow

Every game, the host's too, runs the same `DialogueService`; the host's talks to the bridge over the
in-process route like any player. `HostDialogue` (the bridge rule) decides; games only report and apply.

1. **Start.** A peer activates a copy. The start reaches `Actor::SetDialogueWithPlayer` (37200), which
   `DialogueService` stops and sends instead: `DialogueStartRequest` (the NPC's session id). `HostDialogue`
   refuses it for an actor the host does not own, or while another conversation runs; a start for the NPC
   already in conversation returns silently (the peer joins locally, step 7). Otherwise it tells the host's
   game to start (`NotifyDialogueStart`). That game, unless it talks to someone already (one conversation at a
   time), starts it hidden: `DialogueBridge::Start(npc, false)` skips the menu (52853) and the turn (40496),
   and refuses an NPC fighting or dead (a start from code would run all the same). A start it cannot make goes
   back as `DialogueStartFailedRequest`. Every refusal reaches the asker as `NotifyDialogueRefused` (it
   forgets its ask; kept, the host's next conversation with that NPC opened its menu) and a chat line.
2. **Open.** A force greet reaches the start filter with its flag (37200's `forceGreet`); the host's game marks
   the open `Everyone`, `HostDialogue` puts every player in it and every peer opens its menu on its copy.
   The host's game has a conversation with an NPC with a session id (whoever started it, a
   force-greet too; tracked by `MenuTopicManager`'s speaker, not the menu): its `DialogueService` sends
   `DialogueOpenedRequest` (the NPC). `HostDialogue` opens the conversation (a new id, step 1; the host
   turning to another NPC ends the old one first) and sends `NotifyDialogueOpen` to everyone. The peer that
   asked is in it and opens its menu on its copy (`SetDialogueWithPlayer`, past the hook), or leaves when that
   fails; the others only learn the id. A peer's menu on a copy outside a conversation closes.
3. **Lists.** The host's game sends its engine's list for the step (`DialogueOptionsRequest`: conversation,
   step, whether it is a line's choices, per option its topic, info, quest and text) once per step: at the open,
   and after a pick once its engine shows the next one (52857: the NPC has answered). `HostDialogue` keeps it and
   sends it (`NotifyDialogueList`) to the games in the conversation and to each one joining. The host's menu shows
   its own list; a guest's shows the host's (`DialogueBridge::ShowList`), in place of its engine's own.
4. **Pick.** `TopicClicked` (51509) is hooked on every game. A pick sends `DialoguePickRequest` (conversation,
   step, the option's topic and line, its text); a guest's own menu does not say it. `HostDialogue` takes the
   first pick of a step that is an option of the host's list (topic and line both) and sends
   `NotifyDialoguePicked` (the next step, topic, line, the picker) to everyone, and a chat line
   (`kPlayerChat`: the picker's name, the option's text, through `ChatText` as chat). The host's game says it
   unless its own menu did (`DialogueBridge::PickOption`: the listed option, as a click). Any other pick (a stale
   step, one from a guest's menu before the host's list arrived) is not said, and the picker gets the step's
   list back (`HostDialogue::SendList`), so no menu stays in the picked state. The other menus keep their list
   until the next step's.
5. **The line's script.** Each line's fragment is queued in `TESTopicInfo::Begin` (25616), at its start and
   its end. On the host's game, after a guest's pick, the bridge's `LineFor` filter names that guest's
   character for the conversation's speaker (`HostConversation`, until the next pick; the host's own click clears
   it), and the hook queues the fragment under rule 2's cause (`GuestScripts::ScopedCause`), so the script and
   everything it starts run for the picker. Natives on the player in it (`Game.AdvanceSkill`,
   `IncrementSkill(By)`, `IncrementStat`, `ShowTrainingMenu`) are skipped there and run on the picker's game
   (`PlayerNativeRequest` → `NotifyPlayerNative`; `docs/re/systems/papyrus-guest-scripts.md`).
6. **Leave and end.** The peer who asked for the conversation is in it from the open. Any other game's menu
   open on the conversation's NPC sends `DialogueJoinRequest`; its menu closing (Tab, Esc) sends
   `DialogueCloseRequest`: that player left. `HostDialogue` keeps the players in it; when the last one leaves
   it ends the conversation and sends `NotifyDialogueClosed` to every game, the host's too, whose conversation
   then ends without its menu. While players remain, the host's menu closing does not end its conversation
   (the hook on 35272). The host's engine ending it (a goodbye, the NPC walking off, combat) sends
   `DialogueCloseRequest` with `Ended`, which closes every menu. A conversation whose speaker was deleted (a
   copy left behind on travel, a removed NPC) is cleared on that game (`DialogueBridge::ClearOrphaned`): the
   engine's end does nothing without a speaker and left `menuOpen` set, after which that game started no
   conversation at all. The asker whose menu never opens leaves after 60 frames.
7. **Join.** Talking to the NPC of the conversation under way (`TESNPC::Activate` 24715, where activating
   the current speaker would otherwise do nothing) opens the menu on it: a peer starts its own on its copy,
   the host shows its menu on the background conversation (`DialogueBridge::ShowMenu`).
8. **Training.** A trainer's line calls `Game.ShowTrainingMenu(akSpeaker)`: run for a guest (5), it opens on
   that guest's game with its copy of the trainer, over its dialogue menu, which takes the conversation back
   when the training menu closes, as on the host's game.

### A peer's game

Built (2026-10-14), for any copy, not only in a conversation (the copy contract's voice row):

- `TESTopicInfo::Begin` (25616) does nothing for a copy speaker: no fragment, no "said" flag. Quest and
  world effects come from the host (and, with quest sync, its quest state).
- A copy speaks and shows only its owner's lines: `HookSpeakSoundFunction` (37542) and `HookShowSubtitle`
  (52626) refuse the engine's own on a copy; `NotifyDialogue` and `NotifySubtitle` call the originals.
  Without them a peer's menu on a copy greeted with the peer's own line.
- Its engine never picks: a pick goes to the host. Its menu shows the host's list
  (`DialogueBridge::ShowList`: each option built from its topic and line by the `Dialogue` constructor 35254, no
  conditions tested, the list replaced with `MenuTopicManager::SetOptions`), and gets voice ready (52858), so it
  takes a pick 750 ms later, as after a line. Until the host's list arrives it shows its own engine's; a pick from
  that is not in the host's list and is not said (the host sends its list back). Its engine still builds and
  shows lists of its own (at the open, after the copy's greeting): at that show (52857) the host's list for the
  step goes in their place (the `List` filter), and a menu already showing it is not filled again (a refill takes
  no pick for 750 ms, so a click then was lost).

## Messages

All carry the conversation id and step where they apply; forms go as `GameId`.

| Message | From → to | Content |
|---|---|---|
| `DialogueStartRequest` | game → host | NPC session id |
| `NotifyDialogueStart` | host → host's game | NPC session id |
| `DialogueOpenedRequest` | host's game → host | NPC session id |
| `NotifyDialogueOpen` | host → all | conversation, NPC session id |
| `DialogueOptionsRequest` / `NotifyDialogueList` | host's game → host → all | conversation, step, choices, options (topic, info, quest, text) |
| `DialoguePickRequest` / `NotifyDialoguePicked` | game → host → all | an option of the host's list (topic, line), its text / the next step, topic, line, picker; a pick not in the list is not said |
| `NotifyChatMessageBroadcast` | host → all | the pick: `kPlayerChat`, the picker's name, the option's text |
| `DialogueJoinRequest` | game → host | conversation: this player's menu is open on it |
| `DialogueCloseRequest` / `NotifyDialogueClosed` | game → host → all | conversation; `Ended`: the host's game ended it, else this player left |
| `DialogueStartFailedRequest` / `NotifyDialogueRefused` | host's game → host → the asker | NPC: its start will not happen (also for the bridge's own refusals) |
| `DialogueOpenedRequest` / `NotifyDialogueOpen` `Everyone` | host's game → host → all | the NPC started it (a force greet): every peer opens its menu |
| `PlayerNativeRequest` / `NotifyPlayerNative` | host's game → host → that guest | a native on the player a script ran for a guest (rule 2; not dialogue's own) |

All in `Messages/SharedDialogue.h`, the option in `Structs/DialogueOption.h`. They are not relays, so
`wire_tests.cpp` does not pin them; the bridge cases send and read each one through the server.

A refused start goes back as a session event (a chat system line), like a refused join.

## Engine pieces

Typed structs in `Code/client/Games/` with `static_assert`ed offsets (CommonLibSSE-NG, IDA):

- `MenuTopicManager` (`Games/Misc/MenuTopicManager.h`), `TESTopic`, `TESTopicInfo`, `UIMessageQueue` and
  `BSUIMessageData` (`Interface/UI.h`).
- `IDialogueBridge` / `DialogueBridge` (`Bridges/`): what `DialogueService` needs from the engine, and
  the hooks.
- `FxDelegateArgs` and `GFxValue` for the SWF's `TopicClicked` callback. The bridge names nothing inside the
  SWF: the menu closes through the engine's own `kHide` message (only the harness reads the SWF's state).

Hooks, each checked against its community name and IDA:

- `Actor::SetDialogueWithPlayer` (37200, built): a start with a copy goes to the host.
- `DialogueMenu` `TopicClicked` (51509, built): a peer's pick goes to the host.
- `TESNPC::Activate` (24715, built): talking to the conversation's NPC joins it.
- Show menu (52853), turn to speaker (40496), show list (52857), the menu-close sink (35272), all built: a
  conversation without its menu, and one that outlives its menu (`dialogue.md`). Show list is also when the host's
  game sends a step's list after a pick.
- `TESTopicInfo::Begin` (25616, built): nothing for a copy speaker; a line's fragment queued for its picker.
- `SpeakSound` (37542) and `ShowSubtitle` (52626), both hooked already: copies refuse the engine's lines.

## Open

1. **Barter, gifts, a follower's inventory** open on the host's game: they belong on the picker's game, with
   the NPC's side sent to the host (What players see).
2. **Quest scenes** where the player answers inside a scene (the scene runs on the host's game; not tried).
3. **Per-player effects of a picked line's script:** it runs for the picker (What players see), so what it does
   to `Game.GetPlayer()` reaches the picker's character, and so does a player the script stored. Natives on this
   player run on the picker's game, a value they return (`QueryStat`) comes from there, and the picker's character
   carries the picker's skills (`FavorDialogueScript.Persuade` sizes its Speech experience from them). Open: what
   the script gives the player to own (`RentRoomScript` gives the bed to the picker's character's base, which the
   engine refuses as an owner: `party-model.md`, "Items and gold", decision open), and barter, gifts and a
   follower's inventory (item 1).
4. **Settled 2026-10-19:** say-once lines, force greets for everyone;
   walking off (no distance rule: travel, the NPC unloading or a goodbye end it), the
   host busy (a menu or combat does not stop a start; a fighting or dead NPC does), a menu that never opens
   (left after 60 frames), a click as the list changes (the list comes back), lines said recently (above).

## Decided 2026-10-22: the host's list, not each engine's

Step sync (built 2026-10-22 overnight: the host sent the line it said, `rootTopicInfo`, and each guest's engine
built the step's list with 35287 for its own player) was replaced the same day, at the user's review, by the
host's list for everyone. Step sync needed two list builders, a pick carrying the picker's own line that the
host's game then said without its menu offering it, held choices for answers only a guest had, and a guest
leaving when its engine had none; and it could not get quest topics right on a guest, which runs no quests
(aliases, running quests). The host's list deletes all of that: one list builder, a pick is "this option of
this step", and the host's engine does what it does for its own player. Scenarios that pin it:
`dialogue-options-are-the-hosts`, `dialogue-pick-says-the-hosts-line`, `dialogue-quest-option-from-host`,
`dialogue-unanswered-question-ends-it`, and `client_shows_hosts_list` (topics, lines and texts equal, in order)
in `dialogue-peer-starts` and at each step of `dialogue-several-questions-deep`.

## Build order

Each step ends with its scenarios passing, and works on its own:

1. **Done.** Messages, `HostDialogue` with bridge cases (`dialogue_tests.cpp`): one conversation, refusal,
   open and close for everyone, first pick wins, the picker's line, the chat line, stale steps dropped.
2. **Done.** Open and close for everyone, copies silent: `dialogue-opens-for-everyone` (the client starts it),
   `dialogue-ends-for-everyone` (each player's Tab). A second start needs a third game or a menu that failed to
   open, so the bridge cases cover it.
3. **Done.** Picks from anyone: `dialogue-pick-reaches-host` (the client picks with E; the host's NPC says it,
   the client's copy voices it, both games show the same next topics).
4. **Done.** The background host, joining by talking, leaving: `dialogue-peer-starts` (the host's game runs
   it without its menu, the client's menu has the host's topics), `dialogue-nobody-moves` (the host 1500 units
   off, nobody moved), `dialogue-host-in-background` (the host walks off and back, joins by talking; a pick
   without its menu), `dialogue-join-by-talking` (the client reads her line from 2000 units, joins by
   talking to the copy), `dialogue-last-one-out-ends-it` (either player leaves first).
5. **Done (2026-10-22, the host's list).** `dialogue-options-are-the-hosts` (Brenuin's coin: listed for the client
   when the host carries gold, its pick took the host's gold until step 8, and both chats read `<client>: Here. Have a gold
   piece.`; not listed when only the client carries gold), `dialogue-pick-says-the-hosts-line` (Hulda's "rent a
   room": the client's pick says the host's line, the host does not pay), `dialogue-quest-option-from-host` (the client's
   own beggar quest stopped, its menu still lists the coin), `dialogue-unanswered-question-ends-it` (Onmund's
   question the host's Nord cannot answer ends it for both), `dialogue-picker-gets-speech-experience` (the client persuades Ungrien:
   its Speech gains the experience, not the host's; the host's own persuade stays the host's).
6. **Done.** Loose ends (2026-10-19): `dialogue-picker-trains` (Amren's training opens on the client's game, the
   client trains once, the host's One-handed and gold unchanged), `dialogue-refused-start` (an NPC in combat:
   refused with a chat line, the ask forgotten), `dialogue-said-line-waits` (the coin not offered again within
   its 4 hours), `dialogue-ends-on-travel` (the party travelling ends it for both; the client talks again
   after), `dialogue-npc-removed` (the NPC deleted mid-conversation; the client talks to the next one),
   `dialogue-npc-starts` (a force greet; one during a peer's conversation waits),
   `dialogue-several-questions-deep` (Andurs, three questions by turns; no menu offers another list while he
   answers). Bridge cases for refusals and the list given back. Three players: the bridge cases only (a
   three-game duo is in `docs/ideas.md`, Testing).
7. **Done.** Answers, said lines, greetings (2026-10-19): `dialogue-answer-only-a-peer-has` (Onmund's question
   only a non-Nord can answer: the Nord host's menu closes, the Breton client answers; alone, nobody can and it
   ends), `dialogue-said-once-line` (Hulda's College line not offered again on any game),
   `dialogue-npc-starts` (a force greet opens every game's menu).
8. **Done.** A picked line runs for its picker (2026-10-22, What players see; replaced the Speech and training
   reroutes): `dialogue-options-are-the-hosts` (the client's coin, not the host's), `dialogue-pick-says-the-hosts-line`
   (the host does not pay the client's room), `dialogue-picker-gets-speech-experience` (a real persuade, Ungrien's),
   `dialogue-picker-trains` (the training menu over the client's dialogue menu, back to it after). Bridge case: a
   player native goes to its guest only from the host's game (`relay_tests.cpp`); TPTests round trip.
9. Barter, gifts and a follower's inventory on the picker's game (its own design).

The harness reads each game's conversation (`dialogue`: open, speaker, options; the SWF's pick state and
highlight), its player's skills (`skill`), trainings this level (`trainings`, set back after a scenario) and
what its camera shows (`camera`), makes an NPC force-greet (`force_greet`), and drives it with real keys (E,
W and S, Tab). A scenario waits for the SWF to take a pick (`bAllowProgress`, `eMenuState`) before pressing E, as
a player waits for the line.
