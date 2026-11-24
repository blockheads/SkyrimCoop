# Dialogue stress testing (built 2026-11-24)

The shared conversation ([shared-dialogue.md](shared-dialogue.md)) is tested by 35 hand-written GameTests cases and
about 120 duo scenarios. Each one plays a single ordering someone thought of. The bugs players hit came from orderings
nobody wrote down: a guest leaving with its menu open crashed its game (`re/systems/dialogue.md`, "A guest leaving
mid-line"); the playtest's layered voices came from a menu reopening mid-line. This plan adds a randomized walker over
the in-process games that checks rules which must always hold, and a short duo soak for what the fake cannot show.

## 1. The walker (GameTests)

```
seed -> Party (host + 1 or 2 guests), one NPC with a scripted conversation
loop 200 steps:
  action = pick(seed)            one of the table below, on a random game
  frames = pick(seed, 0..3)      sometimes none: two actions in one frame
  run frames
  every few steps and at the end: settle, then check the invariants
failure -> the seed, the step and the actions so far, printed as a C++ list
           that pastes into a named regression case
```

| Action | What a player or the world does |
|---|---|
| talk | talks to the NPC (starts, or joins the one under way) |
| leave | closes its menu (Tab) |
| pick | presses an option of its list (its own or another's) |
| skip | presses through the line |
| highlight | moves the highlight (W/S, or the mouse on its own menu) |
| kill | the NPC dies (any game's player, or the host's world) |
| combat | the NPC starts a fight |
| delete | the host's world deletes the NPC (a quest) |
| drop | a guest's connection drops |
| rejoin | a guest that left joins again |

Invariants (after settling):

- **One conversation.** Every open menu is on the host's conversation, at its step, showing the party's list.
- **Said once.** Each pick the host accepted is said once by the host's engine, its script run once, for the picker.
- **Nobody stuck.** No menu stays open on an NPC that is dead, deleted or gone; every player can leave.
- **The host's roster is true.** The players the host lists in the conversation are the games with a menu open on it.
- **No engine fault.** No speaker without a process is updated (`FakeDialogue::EngineFrame`).
- **Quests agree.** Every quest's stage and objectives read the same in every game.

What the fake learns first (each from the engine, read in IDA and measured on the duo):

- the speaker dying mid-conversation, and starting a fight: what ends the conversation and when;
- a quest stage a pick sets (the info's script fragment), so "said once" and "quests agree" can be checked.

In `check.sh`: a fixed set of seeds (seconds). On demand: `GameTests "[dialogue-walk]" --long` for thousands, and
under `COOP_NET` (delay, loss) for the races two machines have.

## 2. The duo soak (scenarios, run on demand)

What only Skyrim shows: real voices, real menus, real quest scripts. Lucan's claw conversation (MS13), both players in
it, each checked with the voice log (`Voice:`, every start by actor and file), `speak_calls`, the dialogue desync
check and the quest stages on both games:

1. the guest kills Lucan mid-answer; the host kills him with its menu open;
2. Lucan attacked mid-conversation (a fight starts);
3. each player leaves and talks again, five times quickly, mid-answer;
4. both players pick in the same frame (once the same option, once different ones);
5. a guest leaves and rejoins the session mid-conversation;
6. the claw quest's start and stage from a pick, read on both games after each of the above.

Each finding goes through the bug-fix flow: a fast case first (a walker seed or a named flow), then the fix.

## As built (2026-11-24)

- **The walker:** GameTests `[dialogue-walk]` (`Code/tests/games/dialogue_walk.cpp`): 80 steps a seed, host and one or
  two guests (odd seeds one), every action of the table, the quest moved by "More" (stage + 10, the host's `OnSaid`).
  Six seeds in `check.sh`; `DIALOGUE_WALK_SEEDS=N DIALOGUE_WALK_FROM=S` for more, `DIALOGUE_WALK_STEPS` to find the
  shortest failing prefix, `DIALOGUE_WALK_TRACE=1` for each game's state before the ending, `GAMETESTS_LOG=debug` for
  `HostDialogue`'s roster lines. Under `COOP_NET` the checks wait 2 s for the line (a lost packet's resend).
- **Runs:** 300 seeds clean and 100 under `COOP_NET="delay=60,jitter=20,loss=2"` pass with the fixes below.
- **What it found** (each now a named GameTests flow and host-rule tests):
  1. Seed 198: two asks before the conversation opened. The second replaced the first; the conversation opened with
     nobody listed while the first asker's menu opened, and nothing ended it (the host's NPC kept talking to nobody). A
     single guest pressing talk twice did the same. `HostDialogue` keeps every asker of the pending start.
  2. Seed 1016 (`COOP_NET`): a guest's ask crossed the host's own short conversation with the NPC; the guest took that
     open for its answer and ignored the conversation its ask then opened, which listed it. The open now says whether
     each game is in it (`NotifyDialogueOpen::InIt`), and an ask for the NPC already talking joins that conversation.
  3. Seed 1 (with the voice check): the host's player clicked an option before its game knew the conversation, its
     engine said the answer, and a guest's own ask then replayed that answer over its copy's (`ReplayGreeting` replays
     the last relayed line). Only the greeting's line is replayed now (`VoiceFiles::IsLineOf`).
- **Each line heard once:** the host's engine voices its greeting at each start and every line it says, each a file of
  its own named as the engine names voice files (25502); every line reaches each guest's copy at most once (the
  greeting twice: a guest's own ask replays it once by design), and every line voiced while a guest was in the session
  with a copy of the speaker reaches it. Each guest at the host's step; a world action's result checked at once. The
  fake's speaker keeps its line after its conversation ends until the line ends (a start meanwhile says no greeting,
  only for that speaker).
- **What the fake learned:** a speaker that dies or starts a fight ends its conversation (measured on the duo,
  `re/systems/dialogue.md`, "The speaker dies or fights"); a deleted temporary actor's handle resolves until the
  frame's end; `DialogueBridge::Start` refuses a dead or fighting actor; a start's menu sits in the SWF's
  `SHOW_GREETING` until its first list or pick, where Tab only skips (`FakeDialogue::MenuState`).
- **The soak** (tag `dialogue-stress`) compares the NPC's voice starts by file from the `Voice:` log lines, the host's
  engine against the guest's copy (`expect_same_voice_starts`: a replay or a missed line each fails): `dialogue-speaker-killed-mid-answer`, `dialogue-speaker-fights-mid-answer`,
  `dialogue-guest-rejoins-quickly` (it found a guest rejoining mid-answer could not leave: fixed,
  `re/systems/dialogue.md`, "A menu joining mid-answer") and `dialogue-lucan-claw-under-stress` (the guest leaves and
  rejoins Lucan's claw conversation mid-answer, hands the claw in: each line once, MS13 the same on both games) pass,
  and `dialogue-guest-talks-again-after-leaving` (the playtest's layered voices: fails 2 of 2 without the greeting-only
  replay, passes 2 of 2 with it; GameTests "A guest leaving mid-answer and talking again at once hears the NPC's answer
  once").
  A double press could not be shown on the duo: its loopback answers within a frame, before a second press.
