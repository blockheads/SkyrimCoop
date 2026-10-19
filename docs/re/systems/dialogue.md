# Dialogue

The player's conversation with an NPC: `MenuTopicManager` (the conversation) and `DialogueMenu` (its
Scaleform menu). For shared dialogue (`docs/design/party-model.md`, "Dialogue"). Verified in IDA on
1.6.1170 (2026-10-14); IDs are AE Address Library IDs.

References: CommonLibSSE-NG `RE/M/MenuTopicManager.h` (layout, member names);
`reference/plugins/skyrimui` (vanilla `DialogueMenu.as`); `CHIM-Dialogue-TTS-Bridge` (holds and replays a
pick); `SkyrimSE_SmartTalk` (rewrites the topic list); `InstantlySkipDialogueNG` (skipping lines).

## The conversation (`MenuTopicManager`, singleton 401099)

| Field (CommonLibSSE-NG) | Offset | What |
|---|---|---|
| `selectedResponseNode` | 0x18 | the list node picked last (set by 35269) |
| `dialogueList` | 0x20 | `BSSimpleList<Dialogue*>*`: the options shown, in menu order |
| `criticalSection` | 0x40 | held around a pick (the `TopicClicked` callback) |
| `speaker` | 0x68 | handle of the NPC talking to the player |
| `skipHeadResponse` | 0xB7 | when set, the list's first node is skipped (35269) |
| `topicHistory` | 0xC0 | topics said, appended by 35283 |

A `Dialogue` (0x58) is one option: `topicText` (the line shown), `parentQuest`, `parentTopicInfo` (+0x30,
the info that will be said), `parentTopic` (+0x38).

| ID | RVA | Name (ours) | What |
|---|---|---|---|
| 19524 | 0x2da320 | `TESObjectREFR::SetDialogueWithPlayer` (meh) | `(speaker, start, ?, topic)`. Start: picks a greeting (the speaker's current one, 35286, or a Story Manager event 7/79) and calls 35282; stop: the player's dialogue look ends. **Starts the player's conversation from code**; no distance check on this path |
| 35282 | 0x5e0840 | `MenuTopicManager::StartDialogue` | Called only from the two `SetDialogueWithPlayer`s. Under the critical section: ends any conversation (35294), begins the greeting's info (25616) and says it (35283), builds the list (35274, 35287), reads the speaker's name for the menu |
| 35269 | 0x5dfa50 | `MenuTopicManager::SelectDialogue` | `(manager, index)`: walks `dialogueList` to `index` (past the head when `skipHeadResponse`), stores it in `selectedResponseNode`; for a non-actor speaker sets its topic. The pick's first half |
| 35283 | 0x5e0f10 | `MenuTopicManager::SaySelected` | Says `selectedResponseNode`'s info: begins it (25616), refreshes the speaker (35296), appends its topic to `topicHistory`, plays the response (35285) or waits for the speaker's line to end |
| 25616 | 0x3ebac0 | `TESTopicInfo::Begin` | `(info, stage, speakerHandle*, anySpeaker)`. **Hooked:** nothing for a copy speaker (its owner's game runs the line). Sends the info's script event (`ScriptEventSourceHolder` +0xEC8, `TopicResultCallBack`: **runs the info's fragment**), marks it said (`+0x3A`, change flag `0x80000000`), and for a Speech-weighted info (`+0x3E`) a call on the player (`sub_140749670`, likely Speech XP) |
| 35274 | 0x5dfc90 | (list build, to confirm) | From 35282 and 35294; calls 35275 |
| 35287 | 0x5e1830 | list build | From 35282, 35285, 35290, 35297; clears the list (35275), then the said info's choices or the speaker's topics (35276). See "Building options" |
| 35294 | 0x5e1e50 | `MenuTopicManager::EndDialogue` | From `UpdateInDialogue` (both), `EndInterruptPackage`, the `PositionPlayerEvent` sink, 35282. Logs " %s  walkaway topic is running"; begins an info (25616, the goodbye or walkaway) and rebuilds the list (35274). Its conditions (distance, travel) still to read |
| 35293 | 0x5e1dd0 | | The speaker as an `Actor` (handle +0x68, form type 62) |
| 37200 | 0x660410 | `Actor::SetDialogueWithPlayer` (CommonLibSSE-NG vfunc 0x41) | `(actor, start, forceGreet, topicInfo)`, the actor override of 19524 (the vtables of `Actor`, `Character` and `PlayerCharacter` all point here). Activating an NPC and force greets come here; with `start` it calls 35282. **Hooked** (`DialogueBridge.cpp`): a start with a copy goes to the host instead; returning false is the engine's own "did not start" |
| 35272 | 0x5dfbd0 | `MenuTopicManager::ProcessMenuOpenClose` | The `MenuOpenCloseEvent` sink: the Dialogue Menu closing with a speaker sets `shutMenu` (+0xB3), which ends the conversation. So a `kHide` of the menu (`UIMessageQueue::AddMessage` 13631) ends it from code |
| 37548 | 0x67c240 | `Actor::InitiateDialogue` (meh) | Not the player's menu: an AI dialogue package (type 28) between two actors (an NPC walking up to talk) |

## The menu (`DialogueMenu`, vtable 0x18f6148)

- Slot 1 `Accept` (51507, 0x914690) registers the SWF's calls: `SkipText` (0x914840), `TopicClicked`
  (0x914880), `FadeDone`, `CloseMenu` (0x914980), `PlaySound`. CHIM swaps `TopicClicked` here by wrapping
  the `CallbackProcessor` (no Address Library ID needed).
- Slot 4 `ProcessMessage` (51506, 0x9142b0). SmartTalk rewrites `MenuTopicManager::dialogueList` here on
  `kShow`, `kReshow` and `kUpdate`, before the menu fills the SWF; it replaces the list object
  (`BSSimpleList`) with a reordered one.
- `0x914de0` (no ID): fills the SWF from `dialogueList` (`PopulateDialogueList`), calling 35267, 35268,
  35270, 35295. An entry with its byte +0x4A set is not listed, but still counted: the `topicIndex` the SWF
  gets (and hands back to `TopicClicked`) is the position among all entries, hidden ones included. Then the
  selected index (35295, or -1 when the list is the same as before), and `ShowDialogueList(newList, ...)`.
- `CloseMenu` (0x914980, the SWF's exit): sets `byte_1431AD0EE`, a player flag (+0xBE4 bit 2) and posts a
  message to the menu; `ProcessMessage` on `kHide` with no data leaves dialogue mode and closes at once
  (with data whose +0x20 is set, it plays `StartHideMenu` first).
- `TopicClicked(index)` (0x914880): with the critical section held, `SelectDialogue(index)` (35269) then
  `SaySelected(0, 0)` (35283). `index` is the position in `dialogueList`.

The SWF (`DialogueMenu.as`): the engine calls `PopulateDialogueList` with `(text, isNew, topicIndex)`
triples and the selected index last; the SWF calls `TopicClicked(topicIndex)`, `SkipText`, `CloseMenu`.
**The highlight lives only in the SWF** (`_root.DialogueMenu_mc.TopicList.selectedIndex`,
`SetSelectedTopic`); the engine never hears of it, so showing a pick sets it through Scaleform (a shared cursor
did too, built and dropped 2026-10-19).
CHIM reads the list as `_root.DialogueMenu_mc.TopicList.entryList` and replays a held pick by calling the
original callback with a built `FxDelegateArgs` (`GFxValue{index}`, the menu, its movie).

## For shared dialogue

| Need | Answer so far |
|---|---|
| Start the host player's conversation with an NPC | `SetDialogueWithPlayer(npc, true, ...)` (19524). Limits (distance, the NPC's state) to measure in game |
| Read the list | `dialogueList` after the menu's show/update (SmartTalk's hook point) |
| Replace a peer's list | done: `MenuTopicManager::SetOptions` with options built from the host's list, then 52857 (below, "Building options") |
| Read and set the cursor | Scaleform: `TopicList.selectedIndex` / `SetSelectedTopic` (showing a pick only) |
| Intercept a pick | swap `TopicClicked` in `Accept` (CHIM) |
| Pick on the host | the original `TopicClicked(index)`, or 35269 + 35283 under the critical section |
| A peer's option the host lacks | done: built with 35254 (below, "Building options") |
| Peers run no dialogue scripts | 25616 is the one place an info's fragment starts: skip it on peers |
| A peer follows the host into a branch | not needed: a peer's engine never picks; its menu shows the host's next list (`shared-dialogue.md`) |
| Dialogue with a copy | works (below): activating a copy opens the menu with the copy's own list |
| The copy's voice in a peer's menu | the menu says a line through the speaker's `UpdateInDialogue` (vtable +0x260, from 35285) → `Actor::SpeakSound` (37542, our `HookSpeakSoundFunction`): a copy can refuse every line but its owner's (`NotifyDialogue` calls past the hook) |
| What ends a conversation | 35294, from `UpdateInDialogue`, travel (`PositionPlayerEvent`) and an interrupt package ending; its distance rule still to read |

## In game (2026-10-14, the duo in QASmoke, harness `dialogue`)

Lydia placed by the host (`spawn_host_npc`), each player 110 units from her (its own game's actor):

1. **The host activates her** (`activate`, as E): the menu opens, `speaker` her, one option: "Follow me. I
   need your help." (topic `B0EE6`, info `B0EE9`, quest `5A6DC` DialogueFollower).
2. **The client activates her copy:** the client's menu opens the same way, the copy its speaker, the
   same option computed by the client's own engine (screenshot: Lydia's name, the option, Tab Exit). A
   peer's game builds its own list against a copy.
3. **The client picks locally:** its engine says the line on the copy itself ("I will remain on guard
   here.", the copy's voice line and subtitle on the client only) and runs the info's fragment there:
   talking to the copy again, the client's list is the follower's (Wait here, I need you to do something,
   trade, part ways; quest `750BA`) while the host's Lydia still offers "Follow me". The divergence shared
   dialogue must prevent.
4. **The host picks** with the client's menu open: the host's Lydia says "Lead the way." and the client's
   copy says it within 0.3 s (`NotifyDialogue`, `NotifySubtitle`, existing); the host's menu closes after
   the line (the follower info ends the conversation), the client's stays open.

## Mirror mode, the party's stack, and closing the menu (built 2026-10-14)

- **A copy runs no line of its own.** In a peer's menu on a copy, its own engine picked a greeting, voiced it on
  the copy and showed its subtitle (`copy-ends-voice-line` failed on it). Three guards make a copy say only
  its owner's lines: `TESTopicInfo::Begin` (25616) does nothing for a copy speaker, `HookSpeakSoundFunction`
  (37542) and `HookShowSubtitle` (52626) refuse a copy's own. The owner's relayed lines call the originals
  directly (`Actor::SpeakSound`, `SubtitleManager::ShowSubtitle`).
- **Moving the player during a conversation can end it** (35294 runs from the `PositionPlayerEvent` sink).
  The dropped gathering moved each player (`SetPosition`, a move the party tether does not see) before
  starting with 37200; nobody is moved for a conversation now.
- **Stacked players pass through each other** (the party's arrival) at the collision filter: `bhkCollisionFilter` 78553
  (`CompareFilterInfo(filter, infoA, infoB)`; layer = info & 0x7F, system group = info >> 16, `kNoCollision`
  0x4000) decides every pair, and Precision hooks its seven callers. Hooked once at the function
  (`PlayerCollision.cpp`): two bodies whose groups both belong to stacked players never collide. An actor's
  group is its character controller's (`MiddleHighProcessData::charController` +0x250, vfunc 0x08
  `GetCollisionFilterInfo`), refreshed every frame while the stack lasts. A first try at po3 Tweaks' way
  (clear the bumped body's filter around `Actor::ApplyMovement` 37356) let players stand together in the
  scenario but they still pushed each other in play: it covers only the moving controller's own sweep.
  Off the stack, players collide natively (the closest two walking into each other come is ~40 units); a
  character pushed by another slides around it (78303 adds to its velocity).
- **The engine turns the player to the speaker from where it last saw the player.** Starting the conversation
  in the same frame as the move made both games settle 37 degrees off Lydia, at the bearing from where each
  player stood before the move (harness `camera`: yaw -87, her head at screen x 0.94). Started two frames after
  the move, both settle on her (yaw -51, x 0.50, 5 degrees: the camera looks a little down at her face).
- **Each game's own turn ends somewhere else.** 40496 (a `PlayerCharacter` vfunc, called from
  `MenuTopicManager::StartDialogue`) aims from the player's eyes at the speaker's look-at point
  (+`fDialogueRotationPitchOffset`) and stores a start and target heading and pitch, a duration
  (`fDialogueRotationSecs`) and a done flag (381594, a global bool) next to them; 40497, in the player's
  update, eases the player (third person: the camera) toward the target until the time is up and sets the flag.
  On one spot, with the host's player 45 units from Lydia, the host settled at pitch +17 and the client at -40
  on its copy. In first person the camera's pitch is the player's angle x, and a value set mid-conversation
  holds. A peer copying its copy of the host's player's angle every frame (with 381594 set) got identical
  views, but the gathering it served was dropped the same day (`shared-dialogue.md`): each game now keeps
  its own turn, from where its player stands.
- **The host's conversation does not end with distance.** With the client starting it and the host's player
  400, 1000, 2000 and 4000 units from the NPC, the host's menu opened and stayed open (6 s each), and the
  client's listed its options. `fAIInDialogueModeWithPlayerDistance` has no direct reader.
- **A dialogue menu hidden from outside leaves the HUD without subtitles.** The menu turns the HUD's general
  subtitles off as it opens (`DialogueMenu::ProcessMessage` 51506 → 51516: HUD message
  `kSetSubtitlesEnabled` 9) and back on in its `CloseMenu` callback (51511), which the SWF's `StartHideMenu`
  calls. A `kHide` message without data skips the SWF, so every later general subtitle stays in
  `SubtitleManager` (its current speaker set) but never shows. `DialogueBridge::CloseMenu` calls
  `_root.DialogueMenu_mc.StartHideMenu` on the menu's movie (`GFxMovie::Invoke`, vfunc 0x17), as a player's
  exit does, once per menu (a second call restarts the fade-out). The scenarios' cleanup presses Tab.
- **Subtitles past `fMaxSubtitleDistance`:** each `SubtitleInfo` has `forceDisplay` (+0x1C), the 4th argument
  of `ShowSubtitle` (52626); with it set the update (the HUD's, from the player's update) skips the distance.
  The engine sets it for some of its lines (`Actor::UpdateSoundCallback`); `NotifySubtitle` carries it, so a
  peer shows the host's NPC's line as the host's game does, at any distance.
- **QASmoke's hall has no navmesh** where scenarios put their NPC (cell 32AE7: no triangle under -819, 2065).
  An NPC off the navmesh gets the player's spot at its own height; on the navmesh the way to the spot must be
  on it.

## A conversation without its menu, joins, and picks from the menu (built 2026-10-18)

The host's game runs a peer's conversation in the background (`shared-dialogue.md`). The pieces, checked in IDA
on 1.6.1170:

| ID | RVA | Name (ours) | What |
|---|---|---|---|
| 52853 | 0x976d60 | show menu | `(speakerName)`: queues the Dialogue Menu's show with the name; from `StartDialogue` 35282, which reads it with `TESObjectREFR::GetDisplayFullName` (19781). **Hooked:** skipped for a hidden start. `DialogueBridge::ShowMenu` calls it, then 52857 and 40496, to open the menu on a conversation already under way |
| 52857 | 0x9770c0 | show list | `(newList)`: asks the menu to show the list. **Hooked:** without the menu, sets `shutMenu` on `forceGoodbye` or when no option is left (35267), which the menu's fill does otherwise |
| 52858 | 0x977180 | voice ready | Queues a message to the Dialogue Menu that becomes the SWF's `NotifyVoiceReady`. From `Actor::UpdateInDialogue` (37541, 0x67a140) once the speaker's line has its voice, and from the menu's own close sink (51512) |
| 51509 | 0x914880 | `DialogueMenu` TopicClicked | The SWF's `TopicClicked(topicIndex)` (`FxDelegateArgs`, `Interface/FxDelegateArgs.h`). **Hooked:** a peer's pick goes to the host instead |
| 51511 | 0x914980 | `DialogueMenu` CloseMenu | The SWF's exit; gives the HUD its subtitles back |
| 51512 | 0x914a50 | `DialogueMenu`'s own `MenuOpenCloseEvent` sink | Another menu opening over it: hides or shows the dialogue menu (a message with `InterfaceStrings`+0x228, "DMFOStr", byte +0x20 1 or 0); the menu reshown with a speaker sends voice ready (52858) |
| 51515 | 0x914de0 | list fill | Fills the SWF (`PopulateDialogueList`); sets `shutMenu` itself after a goodbye or with nothing left to say |
| 51516 | 0x915500 | | From `ProcessMessage` (51506) as the menu opens: the HUD's general subtitles off (HUD message 9) |
| 40496 | 0x737310 | turn to speaker | `PlayerCharacter`, called only from 35282. **Hooked:** skipped for a hidden start; 40497 (0x7375e0) eases the turn |
| 35267 | 0x5df9a0 | first option | The list's first option node, or none: whether anything is left to say |
| 35272 | 0x5dfbd0 | `MenuTopicManager::ProcessMenuOpenClose` | **Hooked:** while players remain in a shared conversation, the menu closing does not set `shutMenu`: the menu goes, the conversation stays |
| 24715 | 0x3b9500 | `TESNPC::Activate` | The player activating an NPC. Activating the current (or last) speaker otherwise does nothing: the engine sees it already in conversation, and a copy never leaves that state (it runs no AI). **Hooked:** joins are decided here |
| 37542 | 0x67a9d0 | `Actor::SpeakSound` | Returns a **float** (the line's length; -1 for no voice), not a bool. A copy's refused line returns -1, which `UpdateInDialogue` (37541) takes as said and then sends voice ready (52858) |

A pick is `MenuTopicManager::Pick`: the critical section, then 35269 and 35283, as `TopicClicked` does.

**The menu takes a pick only when the SWF allows it** (`DialogueMenu.as`): `onItemSelect` does nothing until
`bAllowProgress`, set 750 ms after `NotifyVoiceReady`, and only with `eMenuState` 1 (`TOPIC_LIST_SHOWN`). On
the keyboard **E** (Activate, the menu's Accept) selects; Enter does nothing, in a normal conversation too
(2026-10-18, both games, with `bAllowProgress` set and the list shown). The harness `dialogue` command reads
`bAllowProgress`, `eMenuState` and `TopicList.selectedIndex` through `GFxMovie::GetVariable` (vfunc 0x11).

**The highlight** (2026-10-18). The list (`DialogueCenteredList.as`) keeps it in `iSelectedIndex`, an index of its
entries; each entry's `topicIndex` is its index in `dialogueList`, so
`_root.DialogueMenu_mc.TopicList.selectedEntry.topicIndex` is the highlighted option. To set it from code as the keys
do: `SetSelectedTopic(topicIndex)` (only scrolls: sets `iScrollPosition` to the entry), then `iSelectedIndex` to
that position (`GFxMovie::SetVariable`, vfunc 0x10), then `RestoreScrollPosition(position, true)` and `UpdateList()`.
Recentered, the list selects what it is scrolled to; `iSelectedIndex` must be set first, or the entries above the
middle are drawn with the old highlight (`UpdateList` draws them before it moves the selection;
`moveSelectionDown` sets it first). On the keyboard W and S move the highlight. The arrow keys' scan codes (0xC8,
0xD0) do nothing when the harness sends them (not looked into).

## Building options (2026-10-18)

For per-player options (`shared-dialogue.md`, "A peer's option on the host"). Found through Dynamic Dialogue
Replacer (`reference/plugins/Dynamic-Dialogue-Replacer`, `src/Hooks/Hooks.cpp`), which hooks the two calls to
`AddTopic` to hide, replace or add topics while the engine builds the list. Checked in IDA on 1.6.1170.

| ID | RVA | Name (ours) | What |
|---|---|---|---|
| 35303 | 0x5e28e0 | `MenuTopicManager::AddTopic` | `(manager, topic, parent info, front)`: picks the topic's info whose conditions pass for the speaker and the player (`TESTopic` 25544), builds its option (35254), sets its `currentResponse` (+0x40) to its first response (+0x18), drops it without responses or when the list has it already, then links it in the list (at the front, or at the end while 35287 builds) |
| 35287 | 0x5e1830 | list build | The picked option (`selectedResponseNode`, else `lastSelectedDialogue` +0x38) with choices (its byte +0x10, unless +0x48): `rootTopicInfo` (+0x30) becomes its info and the list its choices (`sub_1403ED950`); otherwise `rootTopicInfo` is null and the list the speaker's topics (35276). `AddTopic` for each. Runs again after a line, so options added from outside are gone after it |
| 35285 | 0x5e1310 | advance | Plays the picked info's next response; with none left, builds the list (35287), shows it (52857 with `rootTopicInfo != null`, so a choice list slides in) and sends voice ready (52858). So the engine's next list comes once the NPC has answered. Keeps its own copy of the picked option in `lastSelectedDialogue` (+0x38) |
| 35254 | 0x5debb0 | `Dialogue` constructor | `(dialogue, quest, topic, info, speaker, flag)`: the topic's text (or the info's override), `parentQuest`, `parentTopic` (+0x38, and +0x50), `parentTopicInfo` (+0x30), and a `DialogueResponse` (35249) for each of the info's responses with the speaker's voice. Leaves `currentResponse` null. No conditions |
| 35319 | 0x5e37a0 | `Dialogue` deleting destructor | `(dialogue, free)`: its responses, then the 0x58 bytes when `free` |
| 35318 | 0x5e3710 | list node delete | Deletes a node and every node after it: unlink first |

**`SaySelected` (35283) says only an option's `currentResponse`:** an option built with the constructor alone
says nothing, and the list is built again at once. `MenuTopicManager::BuildOption` sets it as `AddTopic` does.
Nodes past the list's head are 16 bytes on the game's heap (`Memory::Allocate`), as the engine's.

**Setting the whole list** (2026-10-19, `MenuTopicManager::SetOptions`): the option nodes are freed and the list
chained again (the head is the list's own: cleared, never freed), the options left out destroyed;
`selectedResponseNode` moves with its option (35285 reads it, null is fine: it says from
its own copy at +0x38). With `skipHeadResponse` the head node is the greeting, never touched. A peer's menu shows
its lines for the host's topics this way; a list changed with the menu open is redrawn with 52857 (the fill, 51515), and a peer's
menu then gets voice ready (52858) so the SWF takes a pick 750 ms later, as after a line.

**A topic's line for a player** (2026-10-19, `DialogueBridge::GetLine`): `TESTopic` 25544 (0x3e82f0)
`(topic, bool* out, speaker, player, bool, prevTopic, history)`, as `AddTopic` calls it with `(…, false, prev, null)`.
It needs the topic's quest running (`TESQuest::IsRunning`: enabled, not waiting on a stage) with its dialogue
conditions passing, skips lines said within their reset
hours, tests each line's conditions (`TESTopicInfo` 25615 with the speaker and the player) and returns the first
that passes, or a random one of those passing for a random topic. A peer calls it with its copy as the speaker:
no conversation needs to run there. `TESTopicInfo` (+0x20 `parentTopic`, +0x3A `saidOnce`, +0x3C flags with
`kSayOnce` 1<<2) tells the host whether a line is the topic's and still sayable.

**Showing a pick from elsewhere** (`DialogueBridge::ShowPicked`): `Invoke("_root.DialogueMenu_mc.onSelectionClick")`
after setting the highlight plays the SWF's own click (state `TOPIC_CLICKED`, the picked text kept, the list
hidden); its `GameDelegate.call("TopicClicked")` comes back synchronously into our `TopicClicked` hook, which skips
it then. The engine's next list (52857) shows the list again, as after a click.

**Speech checks in Skyrim.esm** (2026-10-19, read from the plugin): persuade lines test `GetActorValue` (14)
Speech ≥ a global (`SpeechEasy`…`SpeechVeryHard`) on the player, OR'd with `GetEquipped` of the Amulet of
Articulation list; the Persuasion perk sets a quest stage that lowers the globals. Intimidate lines test
`GetIntimidateSuccess` (655, `Actor::WillIntimidateSucceed` 37500: the player's Speech against the NPC's, times
the player's level, perk entry 43). Bribe lines test `GetBribeSuccess` (654). Trainers' "train me" tests the
base Speech (`GetBaseActorValue` 277). All of these are judged by the picker's own game.

**`TESContainer::GetItemCount` (14529) is `(container, item)`.** The inherited wrapper passed no item and read
garbage (the host's gold as -140); fixed 2026-10-18. Its one caller is the harness `item_count`.

The info being said is `MenuTopicManager::currentTopicInfo` (+0x70) while the line plays; `lastTopicInfo`
(+0x78) is not the last line of this conversation (it still held another NPC's info).
