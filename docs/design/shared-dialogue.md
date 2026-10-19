# Shared dialogue

One conversation for the whole party (`party-model.md`, "Dialogue"). Engine findings and the in-game
experiments it rests on: `docs/re/systems/dialogue.md`. Agreed 2026-10-14, the join rules changed 2026-10-18,
each player's own options and lines 2026-10-19; built so far: the messages and `HostDialogue`, the host's
background conversation, joining by talking, leaving, picks from anyone with the picker's own line, picks shown
on every menu and in the chat, the picker's Speech experience (Build order).

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
- **Anyone picks, and speaks for the party with their own line:** the host's game says the picker's line. Every
  other menu shows the pick (the picked text stays, the list goes) when it has that topic, and everyone's chat
  names it: `Lydia: Here. Have a gold piece.` Then every menu shows the host's next topics.
- **One conversation at a time.** Talking to another NPC while one is open does nothing (a chat line says
  someone is talking).
- **Anyone leaves on their own** (Tab, Esc). The conversation ends only when the last player in it leaves,
  or when the host's engine ends it (a goodbye line, the NPC walking off, combat, travel): then every menu
  in it closes.
- **Speech experience is the picker's** (decided 2026-10-19, replacing it going to everyone the same day): a
  persuade's script runs on the host's game, for its player, and the Speech experience it gives goes to whoever
  picked the line. So a party can have its talker.
- **Barter, training, gifts** open on the host's game, where the line is said (untested). Later: say those
  topics on the picker's own game (training then costs the picker's gold and raises the picker's skill); barter
  and a follower's inventory also need the other side's items synced (shared barter, after the shared purse).

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
   `DialogueService` stops and sends instead: `DialogueStartRequest` (the NPC's server id). `HostDialogue`
   refuses it for an actor the host does not own; a start for the NPC already in conversation returns
   silently (the peer joins locally, step 7). Otherwise it tells the host's game to start
   (`NotifyDialogueStart`). That game, unless it talks to someone already (one conversation at a time),
   starts it hidden: `DialogueBridge::Start(npc, false)` skips the menu (52853) and the turn (40496).
2. **Open.** The host's game has a conversation with an NPC with a server id (whoever started it, a
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
   (`DialogueBridge::ShowOptions`).
4. **Pick.** `TopicClicked` (51509) is hooked on every game. A pick sends `DialoguePickRequest`
   (conversation, step, the topic, its menu's line for it, the option's text); a peer's own menu does not say
   it. `HostDialogue` takes the first pick of a step and sends `NotifyDialoguePicked` (the next step, topic,
   the picker's line or else the host's, the picker) to everyone, and a chat line (`kPlayerDialogue`: the
   picker's name, the option's text, through `ChatText` as chat). The host's game says it unless its own menu did
   (`DialogueBridge::PickTopic`, with its menu or without): a line it can say for the topic
   (`TESTopicInfo::CanSayFor`: the topic's, not a say-once line said already) takes the place of the topic's
   option, or is built for a topic it lacks; any other, the topic's own option. Every other game in the
   conversation with that topic on its menu shows the pick (`DialogueBridge::ShowPicked`: the SWF's own
   `onSelectionClick`, its `TopicClicked` call skipped) until the next step's list. A stale step is dropped.
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
   `DialogueCloseRequest` with `Ended`, which closes every menu.
7. **Join.** Talking to the NPC of the conversation under way (`TESNPC::Activate` 24715, where activating
   the current speaker would otherwise do nothing) opens the menu on it: a peer starts its own on its copy,
   the host shows its menu on the background conversation (`DialogueBridge::ShowMenu`).

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
| `DialogueStartRequest` | game → host | NPC server id |
| `NotifyDialogueStart` | host → host's game | NPC server id |
| `DialogueOpenedRequest` | host's game → host | NPC server id |
| `NotifyDialogueOpen` | host → all | conversation, NPC server id |
| `DialogueOptionsRequest` / `NotifyDialogueList` | host's game → host → all | conversation, step, choices, options (topic, info, quest, text) |
| `DialoguePickRequest` / `NotifyDialoguePicked` | game → host → all | topic, the picker's line, its text / the next step, topic, the line said, picker |
| `NotifyChatMessageBroadcast` | host → all | the pick: `kPlayerDialogue`, the picker's name, the option's text |
| `DialogueJoinRequest` | game → host | conversation: this player's menu is open on it |
| `DialogueExperienceRequest` / `NotifyDialogueExperience` | host's game → host → the last picker | Speech experience from the lines said |
| `DialogueCloseRequest` / `NotifyDialogueClosed` | game → host → all | conversation; `Ended`: the host's game ended it, else this player left |

All in `Messages/SharedDialogue.h`, the option in `Structs/DialogueOption.h`. They are not relays, so
`wire_tests.cpp` does not pin them; the bridge cases send and read each one through the server.

A refused start goes back as a session event (a chat system line), like a refused join.

## Engine pieces

Typed structs in `Code/client/Games/` with `static_assert`ed offsets (CommonLibSSE-NG, IDA):

- `MenuTopicManager` (`Games/Misc/MenuTopicManager.h`), `TESTopic`, `TESTopicInfo`, `UIMessageQueue`
  (`Interface/UIMessageQueue.h`).
- `IDialogueBridge` / `DialogueBridge` (`Bridges/`): what `DialogueService` needs from the engine, and
  the hooks.
- `DialogueMenu` (`IMenu`) and enough of Scaleform: `GFxMovieView` (`GetVariable`, `SetVariable`,
  `Invoke`), `GFxValue`, `FxDelegateArgs`. Only showing a pick reaches into the vanilla SWF by name
  (`TopicList`, `onSelectionClick`); a menu mod that renames them leaves the pick unshown, nothing else.

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

## Open, to settle while building (experiments first)

1. **Distance:** the host talks from anywhere (held at 4000 units). Still to see: what ends a conversation
   under way (35294) when a player walks off.
2. **The host busy** (in a menu, in combat) when a peer starts one.
3. **A peer who asks and whose menu never opens** though its start ran: it stays in until its engine's
   conversation with the copy ends. A failed start leaves at once (2026-10-19).
4. **Topics that open a menu** (barter, training, gifts, a follower's inventory): on the picker's game, later.
   Bribes wait for the shared purse (their cost depends on Speech, their gold on the purse).
5. **A click as the list changes:** a peer's pick that is not sent leaves its menu in the picked state until
   the next list (rare).

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
6. Shared barter, with the shared purse (its own design).

The harness reads each game's conversation (`dialogue`: open, speaker, options; the SWF's pick state and
highlight), its player's skills (`skill`) and what its camera shows (`camera`), and drives it with real keys (E,
W and S, Tab). A scenario waits for the SWF to take a pick (`bAllowProgress`, `eMenuState`) before pressing E, as
a player waits for the line.
