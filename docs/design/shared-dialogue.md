# Shared dialogue

**Status (2026-10-19):** the host's rule and messages (`HostDialogue`), copies' own lines and the engine structs
are in. The client side described below (`DialogueService`, `DialogueBridge`, the harness and scenarios) is
parked on tag `archive/shared-dialogue-client` until quest/world sync and AI state on copies, then redone as step
sync ("Next").

One conversation for the whole party (`party-model.md`, "Dialogue"). Engine findings and the in-game
experiments it rests on: `docs/re/systems/dialogue.md`. Agreed 2026-10-14, the join rules changed 2026-10-18,
each player's own options and lines 2026-10-19; built so far: the messages and `HostDialogue`, the host's
background conversation, joining by talking, leaving, picks from anyone with the picker's own line, picks shown
on every menu and in the chat, the picker's Speech experience, training on the picker's game, refused starts,
three players (Build order).

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
- **Each player sees their own options** (decided 2026-10-19): the host's topics for the step, each with the
  line that player's own game gives them (their Speech, perks, the Amulet of Articulation, race, anything a mod
  checks). A topic their player gets no line for is not on their menu; their own topics the host lacks are
  (Brenuin's "Here. Have a gold piece." for the one carrying gold), except among a question's answers, where the
  host's are all there is. Menus can differ in options and order; each player moves their own highlight.
- **Anyone picks, and speaks for the party with their own line:** the host's game says the picker's line. The
  other menus keep their list until the next one, and everyone's chat names the pick: `Lydia: Here. Have a gold
  piece.` Then every menu shows the host's next topics.
- **One conversation at a time.** Talking to another NPC while one is open does nothing (a chat line says
  someone is talking). An NPC's force greet waits too.
- **An NPC that starts a conversation greets the party** (decided 2026-10-19): NPCs run their AI on the host's
  game, so its force greet reaches the host's player, and every player's menu opens on it (the peers' on their
  copies), as for the peer who asks.
- **Each player answers a question with their own answers** (2026-10-19): the host's game sends every choice of
  the line, those its player gets no line for too, and each game shows the ones its player gets. When the host's
  player has none, its game holds the choices (the conversation stays on) and its menu closes; a player with no
  answer leaves, as the game ends it alone, so when nobody can answer it ends.
- **A start the host's game cannot make is refused** with a chat line: the NPC fighting or dead (as talking
  to them is in the game), the host's player talking to someone else. The host in a menu, in combat or far
  away does not stop it (its world paused pauses the lines).
- **Anyone leaves on their own** (Tab, Esc). The conversation ends only when the last player in it leaves,
  or when the host's engine ends it (a goodbye line, the NPC walking off, combat, travel): then every menu
  in it closes. Walking off does not end it (the engine has no distance rule); the party travelling does, and
  an NPC that goes away (deleted, or left behind) ends it on every game.
- **Speech experience is the picker's** (decided 2026-10-19, replacing it going to everyone the same day): a
  persuade's script runs on the host's game, for its player, and the Speech experience it gives goes to whoever
  picked the line. So a party can have its talker.
- **Training opens on the picker's game** (built 2026-10-19): the host's game says the trainer's line and runs
  its script, whose training menu opens on the picker's game instead, with its copy of the trainer; the picker
  leaves the conversation for it. The cost, the skill raised and the trainings left this level are the
  picker's.
- **Barter, gifts, a follower's inventory** still open on the host's game, where the line is said. They need the
  other side's items synced (shared barter, after the shared purse).
- **A line said is said on every game:** each game marks a picked line as the host's saying it does, so a
  say-once line (Onmund's greeting, Hulda on the College) is not offered again, and one with reset hours (a
  beggar's coin: 4 hours) not until they pass.

## The rule

**Judge where the player is, act where the world is** (decided 2026-10-19). Skyrim's lines test "the player":
each player's own game has that player's real state, so it judges that player's lines; the host's game, which
has the world and the quests, says the line and runs its script. This replaced the host's player being the one
judged (a peer's Speech, perks or amulet never counted) and an override that read the player's Speech as the
party's best while the host built its list (built and dropped the same day: every other trait, and every mod's,
would have needed one too).

**Only the host's game runs the conversation; every other game is a screen** (decided 2026-10-19, replacing
every game following each pick on its own engine). A peer's engine never picks, says a line or runs a fragment;
it only judges its player's lines (TESTopic 25544 with its copy as the speaker).

**Each menu is its own** (decided 2026-10-19, replacing one shared list with peers' options named `[Name]` and
a shared highlight, built and dropped the same day to keep it simple; a later piece if wanted).

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
   step, whether it is a line's choices, per option its topic, info, quest and text) after every change, and
   after a pick only once its engine shows the next one (52857: the NPC has answered). `HostDialogue` sends it
   to everyone (`NotifyDialogueList`). The host's menu shows its own list. A peer's game works out its lines
   once per list (`DialogueService::OwnLines`): each topic with its player's line (`DialogueBridge::GetLine`),
   left out when it gets none; a topic whose quest does not run in that game is the host's world, so it keeps
   the host's line (until quest sync); then, unless the list is a line's choices, its own topics the host lacks
   (its engine's list from its menu opening on the copy). Its menu shows them in place of its engine's own
   (`DialogueBridge::ShowOptions`). For a line's choices the host's game sends them all in order (25659, the
   choices of `rootTopicInfo`), with no line where its player gets none; a peer shows those its player gets a
   line for, and leaves when there is none. The host's game, its player with no choice, holds them as unlisted
   options (`Dialogue` +0x4A) with their topic's first line, so the engine does not end the conversation, and
   closes its menu instead of filling it (the fill ends it on a list with nothing to show); a peer's pick says
   its own line in the held one's place. The menu is drawn again for each new step's list even when its engine built the
   same one. Its engine's own list show (52857, after the copy's line) waits while the next step's list has
   not come: shown, it offered the peer's own topics, pickable, while the host's NPC still answered.
4. **Pick.** `TopicClicked` (51509) is hooked on every game. A pick sends `DialoguePickRequest`
   (conversation, step, the topic, its menu's line for it, the option's text); a peer's own menu does not say
   it. `HostDialogue` takes the first pick of a step and sends `NotifyDialoguePicked` (the next step, topic,
   the picker's line or else the host's, the picker) to everyone, and a chat line (`kPlayerDialogue`: the
   picker's name, the option's text, through `ChatText` as chat). The host's game says it unless its own menu did
   (`DialogueBridge::PickTopic`, with its menu or without): a line it can say for the topic
   (`TESTopicInfo::CanSayFor`: the topic's, not a say-once line said already) takes the place of the topic's
   option, or is built for a topic it lacks; any other, the topic's own option. The other menus keep their list
   until the next step's (showing the pick on them, through the SWF, was dropped 2026-10-19: `docs/ideas.md`).
   A stale step is dropped: the picker gets the step's list back (`HostDialogue::SendList`), as does a pick on a
   click that was never sent (`Redraw`), so no menu stays in the picked state. A line said within its reset hours
   on the host's game (`PlayerCharacter::SaidRecently`, 40694) falls back to the list's own; a topic with nothing
   left to say leaves the host's list as it stands as the next step's. Every peer remembers the line as heard
   (`MarkSaid`: the line's said flag and change flag, and 40692 for reset hours, as `TESTopicInfo::Begin` does on
   the host's game), so its menu skips a say-once line, and one with reset hours as long.
5. **Speech experience.** On the host's game, Speech experience its player is about to gain
   (`PlayerCharacter::AddSkillExperience`, from any thread; a persuade's script, barter) after another
   player's pick in the latest conversation is not added (`DialogueBridge::SetSpeechExperienceFilter`): it goes
   as `DialogueExperienceRequest` to `HostDialogue`, which sends `NotifyDialogueExperience` to that conversation's
   last picker, or back to the host when the picker left. The picker's game adds it as its own
   (`GainSpeechExperience`: its bonuses apply). The host's own pick, or a new conversation, keeps it the
   host's.
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
8. **Training.** On the host's game, a training menu about to open (52942, `Game.ShowTrainingMenu`'s only
   call) after another player's pick does not open there: `DialogueTrainingRequest` (the trainer) goes to
   `HostDialogue`, which sends `NotifyDialogueTraining` to the last picker (the host's game when the picker
   left). The picker's dialogue menu closes and the training menu opens with its copy of the trainer
   (`DialogueBridge::OpenTraining`). A trainer outside the conversation opens on the host's game as usual.

### A peer's game

Built (2026-10-14), for any copy, not only in a conversation (the copy contract's voice row):

- `TESTopicInfo::Begin` (25616) does nothing for a copy speaker: no fragment, no "said" flag. Quest and
  world effects come from the host (and, with quest sync, its quest state).
- A copy speaks and shows only its owner's lines: `HookSpeakSoundFunction` (37542) and `HookShowSubtitle`
  (52626) refuse the engine's own on a copy; `NotifyDialogue` and `NotifySubtitle` call the originals.
  Without them a peer's menu on a copy greeted with the peer's own line.
- Its engine never picks (2026-10-19): a pick goes to the host, and the next list is built from the host's
  topics. A menu shown a new list gets voice ready (52858), so it takes a pick 750 ms later, as after a line.
- **Until quest sync** its lines are judged with its own quest state: a line with a quest condition can come
  out as that game sees it, and its own quest topics the host lacks show on its menu (the host's game then
  says them). Conditions on the NPC run on its copy, so they are per player too (its relationship with that
  player). Only the host's game knows which lines were said recently (their reset hours); a say-once line
  said there falls back to the host's own.

### An option the host lacks

Built 2026-10-18. The host's list does not have it (its conditions failed for the host's player). The host's
game builds the option as `MenuTopicManager::AddTopic` (35303) does once an info's conditions pass: the
`Dialogue` constructor (35254) with the topic, the info and the speaker, then its first response as the one to
say. `SaySelected` (35283) says it without testing the conditions again. The info's fragment runs on the host,
for the host's player (`dialogue.md`). Peers' menus are built the same way.

## Messages

All carry the conversation id and step where they apply; forms go as `GameId`.

| Message | From → to | Content |
|---|---|---|
| `DialogueStartRequest` | game → host | NPC session id |
| `NotifyDialogueStart` | host → host's game | NPC session id |
| `DialogueOpenedRequest` | host's game → host | NPC session id |
| `NotifyDialogueOpen` | host → all | conversation, NPC session id |
| `DialogueOptionsRequest` / `NotifyDialogueList` | host's game → host → all | conversation, step, choices, options (topic, info, quest, text) |
| `DialoguePickRequest` / `NotifyDialoguePicked` | game → host → all | topic, the picker's line, its text / the next step, topic, the line said, picker |
| `NotifyChatMessageBroadcast` | host → all | the pick: `kPlayerDialogue`, the picker's name, the option's text |
| `DialogueJoinRequest` | game → host | conversation: this player's menu is open on it |
| `DialogueExperienceRequest` / `NotifyDialogueExperience` | host's game → host → the last picker | Speech experience from the lines said |
| `DialogueCloseRequest` / `NotifyDialogueClosed` | game → host → all | conversation; `Ended`: the host's game ended it, else this player left |
| `DialogueStartFailedRequest` / `NotifyDialogueRefused` | host's game → host → the asker | NPC: its start will not happen (also for the bridge's own refusals) |
| `DialogueOpenedRequest` / `NotifyDialogueOpen` `Everyone` | host's game → host → all | the NPC started it (a force greet): every peer opens its menu |
| `DialogueTrainingRequest` / `NotifyDialogueTraining` | host's game → host → the last picker | the trainer whose menu opens there |

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
  conversation without its menu, and one that outlives its menu (`dialogue.md`). Show list also counts the
  lists the engine shows: the host's next list after a pick.
- `TESTopicInfo::Begin` (25616, built): nothing for a copy speaker.
- `SpeakSound` (37542) and `ShowSubtitle` (52626), both hooked already: copies refuse the engine's lines.
- `PlayerCharacter::AddSkillExperience` (hooked already): the host's Speech experience from another's pick
  goes to the picker.
- Open the training menu (52942, built): another player's training opens on its game.

## Open

1. **Barter, gifts, a follower's inventory** open on the host's game: they need the other side's items synced
   (shared barter, after the shared purse). Bribes wait for the shared purse too (their cost depends on
   Speech, their gold on the purse).
2. **Quest scenes** where the player answers inside a scene: with quest sync (until then a peer's quest state
   is its own, and quest topics the host's game has moved past can stay on its menu).
3. **Settled 2026-10-19:** each player's own answers, say-once lines on every game, force greets for everyone;
   walking off (no distance rule: travel, the NPC unloading or a goodbye end it), the
   host busy (a menu or combat does not stop a start; a fighting or dead NPC does), a menu that never opens
   (left after 60 frames), a click as the list changes (the list comes back), lines said recently (above).

## Next: sync the step, not the list (decided 2026-10-19)

**Today a peer's list is rebuilt by hand.** A peer's engine never says a line (the host's game does), so it
stays at the conversation's first step: when the NPC asks a question, its list still has the opening topics.
So the host sends its options each step and each peer replaces its engine's list with them, judged for its own
player (`DialogueService::OwnLines`, `DialogueBridge::ShowOptions` building options, the record of options put
in the list, `SendOptions`, the `List` filter holding back the peer's own list), checked every update.

**Instead, sync the step and let each engine build its own list.** The engine builds the list from one piece of
state, the line just said (`rootTopicInfo`): its choices, or else the speaker's topics, each judged for the
player (conditions, say-once, reset hours, random lines, and any mod hooking `AddTopic`).

- The host's game sends the step when its engine shows a list (`HookShowList`, after 35287), not on each line:
  lines that continue on their own ("invisible continue") are not steps. The first step is the greeting the
  host's NPC said; a player joining later gets the current step with the open.
- Each peer marks the said lines said (say-once, reset hours, topic history), sets the step as its
  conversation's line and calls its engine's own build and show (35287, 52857), as after a line.
- Picks stay as they are: a peer picks from its own engine's list and sends its topic and line; the host's game
  says it (`PickTopic`). `HoldChoices` stays on the host; a peer with no line for any choice leaves, as now.
- It deletes the list sync above, and dialogue mods that edit the list work on every game, not only the host's.

**What it needs from the world.** Each game judges topics against its own state, so that state must be the
host's:

- Quest stages, aliases and globals: quest and world sync (`docs/design/party-model.md`,
  `docs/design/world-state-sync.md`). Until then, quest topics at stages a peer's game lacks are missing from
  its menu (the conversation still works; the host's topics are no longer forced on it).
- The NPC's AI state, read through its copy: copies run no AI but must carry their owner's AI state
  (`docs/design/copy-contract.md`, "AI state"). In Skyrim and its DLC, about 200 of the 15,449 lines under menu
  topics test it on the speaker: `GetIsCurrentPackage` 87, `GetSitting` 32, `GetAlarmed` 27, `IsScenePlaying` and
  `IsInScene` 30, `GetSleeping` 14, `IsInCombat` 9 (`scripts/re/dialogue_conditions.py`; 226 more test
  `IsInDialogueWithPlayer`, which a copy in a peer's conversation already passes).
- Ordering: a line's script can change state the next list reads (a quest stage). A peer applies the synced
  state before building the step's list: the step follows that state on the same ordered channel, or the peer
  builds again when it changes during the step.

**First, a spike:** on the client, a menu open on a copy, set its conversation's line to the host's and call
35287 and 52857; check the list against the host's and what else 35287 reads (the speaker, the greeting head,
`lastTopicInfo`, `topicHistory`).

**Until then, event-driven, not polled:** `DialogueService::ShowOwnLines` runs every update while the menu is
open and relies on `ShowOptions` finding nothing changed. It only needs to run when the host's list arrives,
the peer's engine builds its own list (`HookShowList`) or the step changes. Worth doing only if step sync is not
next, as step sync deletes it.

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
5. **Done.** Each player's own options and lines: `dialogue-peer-only-option` (Brenuin's "Here. Have a gold
   piece." for the client with gold, the host without: only the client's menu lists it; its pick makes the
   host's game say Brenuin's answer, and both chats read `<client>: Here. Have a gold piece.`),
   `dialogue-picker-says-its-line` (Hulda's "rent a room", the host with gold and the client without: each
   menu has its own line, the client's pick makes the host's game say the client's, the host's open menu shows
   the pick, both show the same next topics), `dialogue-picker-gets-speech-experience` (after the client's pick,
   Speech experience on the host's player goes to the client's Speech, not the host's; after the host's own,
   it stays the host's).
6. **Done.** Loose ends (2026-10-19): `dialogue-picker-trains` (Amren's training opens on the client's game, the
   client trains once, the host's One-handed and gold unchanged), `dialogue-refused-start` (an NPC in combat:
   refused with a chat line, the ask forgotten), `dialogue-said-line-waits` (the coin not offered again within
   its 4 hours), `dialogue-ends-on-travel` (the party travelling ends it for both; the client talks again
   after), `dialogue-npc-removed` (the NPC deleted mid-conversation; the client talks to the next one),
   `dialogue-npc-starts` (a force greet; one during a peer's conversation waits),
   `dialogue-several-questions-deep` (Andurs, three questions by turns; no menu offers another list while he
   answers), `dialogue-three-players` (`duo --players 3`: a pick from the second client, one client leaves,
   the last one out ends it). Bridge cases for refusals, the list given back, training.
7. **Done.** Answers, said lines, greetings (2026-10-19): `dialogue-answer-only-a-peer-has` (Onmund's question
   only a non-Nord can answer: the Nord host's menu closes, the Breton client answers; alone, nobody can and it
   ends), `dialogue-said-once-line` (Hulda's College line not offered again on any game),
   `dialogue-npc-starts` (a force greet opens every game's menu).
8. Shared barter, with the shared purse (its own design).

The harness reads each game's conversation (`dialogue`: open, speaker, options; the SWF's pick state and
highlight), its player's skills (`skill`), trainings this level (`trainings`, set back after a scenario) and
what its camera shows (`camera`), makes an NPC force-greet (`force_greet`), and drives it with real keys (E,
W and S, Tab). A scenario waits for the SWF to take a pick (`bAllowProgress`, `eMenuState`) before pressing E, as
a player waits for the line.
