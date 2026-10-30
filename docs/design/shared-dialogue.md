# Shared dialogue

**Status (2026-10-29):** built, the party's list: every menu shows every player's options, tagged with who can say
them, one highlight for the conversation, and each player says only its own options ("Decided 2026-10-29" below); the
lists are judged on the host's game for each player's character (2026-10-28); before that, the host's list for
everyone (2026-10-22). Fast tiers pass; in Skyrim `dialogue-options-are-the-partys`, `dialogue-highlight-is-shared`
and `dialogue-question-answered-by-who-can` (was `dialogue-unanswered-question-ends-it`) pass, with the rest of the
dialogue tag (29 of 30 on the first run: the old question scenario, rewritten). `HostDialogue`, the client side split by role
(`DialogueService`, `HostConversation`, `GuestDialogue`, `DialogueBridge`), `JudgedFor`, the harness and the scenarios.

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
- **Every menu shows the party's list** (decided 2026-10-29; each line its own option since 2026-10-30, the user): the
  host's engine builds the step's list once with the real quests and NPC, and judges every topic for each player's
  character there (its items, Speech, race, factions, perks; the host's for its own). Every menu in the conversation
  shows the same list: every line its players have, in the host's order then the others'. A topic with a line per
  player shows each line, named for who has it (Lucan's claw: `I could help you get the claw back. [Host]`, then
  `Wait, you mean this golden claw? [Guest]`); a line everyone has is not named. Replaced "a topic shows each player's
  own line" (2026-10-29), which left the two menus different.
- **A pick takes every menu's list away** (2026-10-30): every menu in it shows the pick, as the picker's does after
  its click, while the NPC answers; then every menu shows the next list. A menu never shows its own engine's list in
  a conversation: a guest's engine showing one (after its copy's line) waits for the party's.
- **One highlight** (decided 2026-10-29): anyone moves it (W, S, the mouse), every menu in the conversation shows it
  there. Chaotic on purpose.
- **Anyone highlights any option; a player says only its own:** pressing E on an option of its own list says it, its
  own line (its Speech check, its items); the host's game says it, its script run for that player (its items, gold,
  Speech experience and reward), and menus a line opens (training, barter, gifts) open for it. Pressing E on another
  player's option says nothing: only that player's chat reads `I'm sorry Aela, you don't know how to say that.` (its
  own game decides, from the option's names; the host's rule refuses it too). Another can point at it with the
  highlight for its owner to press. Everyone's chat names who said what: `Lydia: I found Lucan's claw.` Then every
  menu shows the next list.
- **One conversation at a time.** Talking to another NPC while one is open does nothing (a chat line says
  someone is talking). An NPC's force greet waits too.
- **An NPC that starts a conversation greets the party** (decided 2026-10-19): NPCs run their AI on the host's
  game, so its force greet reaches the host's player, and every player's menu opens on it (the peers' on their
  copies), as for the peer who asks.
- **A question's answers are the party's** (2026-10-29): an answer only another player has is on every menu, named
  for them, and theirs to say (`dialogue-question-answered-by-who-can`: Onmund's question about Nords; the Nord host's
  press is refused, the Breton guest answers). When nobody in it has one, the conversation ends, as it does in the game.
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

**The host's game judges for each player, and acts** (decided 2026-10-28, the user: Speech checks are the picker's).
Its engine builds each list with the real quests, aliases and NPC, and judges every topic's line for its own player
and for each other player's character there, as if it were the player (`JudgedFor`, below). It says each pick with
the picker's line and script (since 2026-10-29: a player picks only its own options). Every menu, the
host's too, is a screen: it shows the party's list and sends picks.

```text
the host's engine builds the step's list (35287 -> AddTopic 35303 per topic)
  its own player's line                       -> the host's menu, as ever
  each other player's character: JudgedFor(character) { TESTopic 25544(topic, speaker, character) }
      the line it gets, its text rendered as the engine's (built and destroyed)
  each character's list: the host's topics it has a line for, in the host's (sorted) order, then those only it has
show (52857) -> DialogueOptionsRequest { host's list, Players: [{player, list}] }
HostDialogue: the party's list to every game in it; a pick must be on the picker's own list, said with its line
the host's game says the pick: its listed option, else one built for that line (DialogueBridge::PickOption)
```

The character stands in for its player: its copy carries the player's items, factions, race, looks, level, perks
(`PerkService`) and skills (its spawn's values, then the value relay). What reads the player outside the line's
subject and target is rewritten for the character while it is judged (`docs/re/systems/conditions.md`, "Judged for a
character"): a condition run on PlayerRef or a quest alias holding the player, a PlayerRef parameter, the `GetPC*`
functions, and the intimidate and bribe checks (which read `g_thePlayer` inside the engine).

Kept the host's for everyone: which topics are offered at all (the speaker's exclusive branch, one rumor per list) and
the said-line history (say-once lines and reset hours: the user, "say once: sure"). Limits: a guest whose own list is
empty has its menu end (the engine's fill ends a list with nothing to show). A condition on the player's spells
(`HasSpell`) reads the copy's added spells, which carry its player's (`PerkService`).

History: "judge where the player is, act where the world is" (2026-10-19) let each game's engine judge its own player's
lines, but a guest's engine judged quest topics against quest state it does not run; the host's list for everyone
(2026-10-22) fixed that at the cost of every condition being the host's player's (Lucan's claw in a guest's bag offered
no hand-in). Judging on the host for the character keeps the host's quest state and gives each player its own.

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
   The opening also carries the host's selected greeting INFO (2026-10-26, guard arrest): copies do
   not run alarm AI and may find no eligible local greeting. The guest uses the explicit native
   force-greet entry with that INFO, including when joining later.
3. **Lists.** The host's game sends its engine's list for the step (`DialogueOptionsRequest`: conversation,
   step, whether it is a line's choices, per option its topic, info, quest and text) at the open,
   and whenever its engine rebuilds the list (52857), including before a pick. `HostDialogue` keeps it and
   merges it with the players' own lists into the party's list and sends it (`NotifyDialogueList`) to every game in the
   conversation, the host's too, again whenever a player joins or leaves. Every menu shows it
   (`DialogueService::ShowPartyList`, `DialogueBridge::ShowList`), in place of its engine's own.
   The copy receives the host's rendered text too: native option construction otherwise recalculates
   tokens locally, such as showing a 0-gold arrest fine (BSString::Set 439876).
4. **Pick.** `TopicClicked` (51509) is hooked on every game. A pick sends `DialoguePickRequest` (conversation,
   step, the option's topic and line, its text); no menu says it, the host's included. `HostDialogue` takes the
   first pick of a step whose topic is on the picker's own list (`DialogueLineFor`; another's option is refused, the
   picker's chat told why) and sends `NotifyDialoguePicked` (the next step, topic, the picker's line, the picker) to
   everyone, and a chat line (`kPlayerChat`: the picker's name, its line's text, through `ChatText` as chat). The
   host's game says it (`DialogueBridge::PickOption`: the listed option as a click, else one built for the picker's
   line). Any other pick (a stale step, one from a guest's menu before its list arrived) is not said, and the picker gets the step's
   list back (`HostDialogue::SendList`), so no menu stays in the picked state. The other menus keep their list
   until the next step's.
5. **The line's script.** Each line's fragment is queued in `TESTopicInfo::Begin` (25616), at its start and
   its end. On the host's game, after a guest's pick, the bridge's `LineFor` filter names that guest's
   character for the conversation's speaker (`HostConversation`, until the next pick; the host's own click clears
   it), and the hook queues the fragment under rule 2's cause (`GuestScripts::ScopedCause`), so the script and
   everything it starts run for the picker. Natives on the player in it (`Game.AdvanceSkill`,
   `IncrementSkill(By)`, `IncrementStat`, `ShowTrainingMenu`) are skipped there and run on the picker's game
   (`RoutedCallRequest` → `NotifyRoutedCall`, to the owner; `docs/re/systems/papyrus-guest-scripts.md`).
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
- Its engine never picks: a pick goes to the host. Its menu shows its list from the host
  (`DialogueBridge::ShowList`: each option built from its topic and line by the `Dialogue` constructor 35254, no
  conditions tested, the list replaced with `MenuTopicManager::SetOptions`), and gets voice ready (52858), so it
  takes a pick 750 ms later, as after a line. Until the host's list for the step arrives its engine's own shows are
  held (`GuestDialogue::OnListBuilt` returns an empty list; `HookShowList` keeps what the menu shows, 2026-10-30). Its engine still builds and
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
| `DialogueOptionsRequest` / `NotifyDialogueList` | host's game → host → each game in it | conversation, step, choices, options (topic, info, quest, text); the request also each other player's own list (`Players`); the notification the party's list, each option's `Players` (who has it, none: everyone) |
| `DialoguePickRequest` / `NotifyDialoguePicked` | game → host → all | a topic of the picker's own list (topic, line shown, text) / the next step, topic, the picker's line, the picker; another's option is not said (the picker's chat gets `kSystemMessage` "I'm sorry ...") |
| `DialogueHighlightRequest` / `NotifyDialogueHighlight` | game → host → the others in it | conversation, step, the highlighted option's place in the list, who moved it |
| `NotifyChatMessageBroadcast` | host → all | the pick: `kPlayerChat`, the picker's name, its line's text |
| `DialogueJoinRequest` | game → host | conversation: this player's menu is open on it |
| `DialogueCloseRequest` / `NotifyDialogueClosed` | game → host → all | conversation; `Ended`: the host's game ended it, else this player left |
| `DialogueStartFailedRequest` / `NotifyDialogueRefused` | host's game → host → the asker | NPC: its start will not happen (also for the bridge's own refusals) |
| `DialogueOpenedRequest` / `NotifyDialogueOpen` `Everyone` | host's game → host → all | the NPC started it (a force greet): every peer opens its menu |
| `RoutedCallRequest` / `NotifyRoutedCall` (to the owner) | host's game → host → that guest | a native on the player a script ran for a guest (rule 2; not dialogue's own) |

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

## Decided 2026-10-29: the party's list, one highlight, said by who has it

The user, for v1: everyone sees every option with who can say it; one shared highlight; a player says only an option
of its own list, and pressing another's tells only that player "I'm sorry <name>, you don't know how to say that."
(first decided as "the nearest who has it says it", replaced the same day: nobody spends another's items or Speech).
Replaced "each menu shows its own list" (2026-10-28).

```text
host's game: its list + each player's judged list  -> DialogueOptionsRequest
HostDialogue (MergeDialogueLists): the players in it, the host's order then the others', each topic once,
  Players = who has it (none: everyone) -> NotifyDialogueList to every game in it, the host's too, each with its own
  line for a topic it has
a menu: PartyOptionText(text, names) -> DialogueBridge::ShowList; its highlight moved -> DialogueHighlightRequest ->
  NotifyDialogueHighlight to the others in it -> DialogueBridge::SetHighlight
a press on another's option (CanSay: not in its names) -> nothing sent; SessionNoticeEvent CannotSayText(its name)
a press on its own (the host's too: no menu says its own pick) -> DialoguePickRequest{topic}
HostDialogue (DialogueLineFor): the presser's own line for the topic, else refused with the same chat line, to it only
  -> NotifyDialoguePicked{PlayerId = presser} -> the host's game says it, LineFor = the presser
```

- Merged by topic, not by line: a topic with a line per player (a persuade, Lucan's claw) is one option, and each
  player who presses it says its own line, so Speech checks stay the presser's (2026-10-28).
- The highlight is read each update while the list shows (`selectedEntry.topicIndex`): the SWF tells the game nothing
  when it moves (`docs/re/systems/dialogue.md`, "The highlight"). A move another game made is applied once this menu
  shows the same step's list, and is not sent back.
- Tests: `HostAuthorityTests` (`dialogue_tests.cpp`: the merge, its own options said, another's refused with the
  line to the presser only, the highlight relay), `ClientTests` (`PartyOptionText`, `CanSay`, `CannotSayText`),
  `GameTests` (`dialogue_flows.cpp`: each menu's own lines and the tags, the host pressing a guest's option refused on
  its own game, the guest saying its own, the shared highlight with three games).

## Decided 2026-10-28: each player's list, judged on the host

The host's list for everyone made every condition the host's player's. Judging each topic on the host for each
player's character keeps one list builder and the host's quest state, and gives each player its own items, Speech,
race and factions. Scenarios: `dialogue-line-judged-for-guest` (Lucan's claw line judged for the client's character
reads its bag; with `SKYRIMCOOP_DISABLE_HOOKS=JudgedFor` it reads the host's), `dialogue-options-are-each-players` (the
client holding the claw gets Lucan's line for it, the host his line for a player without it; the client picks it, the
host's game says the client's line and the claw leaves the client's bag). GameTests `dialogue_flows.cpp`,
HostAuthorityTests "each player gets its own list".

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
