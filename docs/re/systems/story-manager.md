# The story manager: how quests start (1.6.1170)

How the engine starts quests, and how a guest's game is kept from starting its own (world-state sync,
"Only the host's game decides"). Read in IDA 2026-10-20; names in `docs/re/ida_notes.tsv`. The story events
themselves and forwarding one to the host: `docs/research/player-events.md` ("Spike 4").

## Every way a quest starts

```
story event (68 raise sites: arrivals CLOC, kills, script SendStoryEvent SCPT, crafting...)
  BGSStoryEventManager::AddEvent_Impl 32359        queues it, returns its id
quest start from a script or the console
  TESQuest::EnsureQuestStarted 25003 (quest, out, startNow)
    startNow:  TESQuest::FillAliases 25048, then BeginStartUpQuest          (Papyrus Start, console startquest)
    otherwise: queues a "start this quest" story event (AddEvent_Impl)

BGSStoryTeller::Update 32483, per frame, each queued event in order:
  a queued quest start  -> FillAliases 25048; filled: the quest is picked
  any other event       -> BGSStoryManager::ProcessEvent 32372
                             the event type's node, the story tree, conditions; adds the quests it picks
  each picked quest     -> BGSStoryTeller::BeginStartUpQuest 32485
                             TESQuest::Reset 25014 (stages, objectives, completed/failed cleared), queued
                             for its start-up stage
  BGSEventProcessedEvent {event id, started a quest} to script event sinks (first source of
  ScriptEventSourceHolder); the event leaves the queue unless processing timed out
```

`TESQuest::Reset` has two other callers: Papyrus `Quest.Reset` and the console's `resetquest`.

## A guest's story manager starts no quest

`BGSStoryManager::ProcessEvent` (32372) is hooked (`Games/Skyrim/Misc/BGSStoryManager.cpp`). While the game
is a guest (`GuestWorldState` switches it through `GuestEngine::SetDecidesWorld`), the hook leaves the
tree unwalked and reports "not timed out". `Update` then starts nothing and ends the event with `started
a quest` false, the same path as an event no node takes. The events are still raised, so forwarding them
to the host (Spike 4) can capture them at `AddEvent_Impl`.

Not covered by the gate: a quest started by a guest's script or console (`EnsureQuestStarted`, and its
queued start through `Update`'s first branch). Those go with step 5, a guest's world scripts.

Scenario `guest-story-starts-no-quest`: both players arrive at the Bannered Mare. Each game raises its own
arrival event (`CLOC`). The host's starts quests (the bard's song, the hold's radiant quests); the guest's
is handled with none started. Before the gate the guest's started quests too.

## A guest's story events judged for its character (2026-10-27)

Party model rule 4 (`docs/design/party-calls.md`, "A player's story events"). Read in IDA 2026-10-27:

- `BGSStoryEvent` (CommonLibSSE-NG, 0x38): id, the type's registered index, six members. Built from the raw event by
  32330 (`(BGSStoryEvent*, index, raw)`, called only from `AddEvent_Impl`): a member typed as a handle or an int (type
  0 or 3 in the registered type, 32352) takes 4 bytes of the raw event, any other 8.
- Indexes: KILL 380067, CLOC 380069, CRFT 380076. KILL's raw event (`Actor::KillImpl` builds it, 32282): victim
  handle, killer handle, location, relationship, crime: the killer is member 1.
- `TESActorLocationChangeEvent` {actor, old, new} (dispatcher +0x108) is sent by `TESObjectREFR::MoveRefToNewSpace`
  (19799) and 37313 for any reference, and by `PlayerCharacter::SetParentCell` after it queued its CLOC (`AddEvent_Impl`
  first, then the script event).
- The visitors' parameters: 32429 at +0x19D and 32430 at +0x3F call `ConditionCheckParams`'s constructor (29871:
  params, subject, target, owner form) with `g_thePlayer` twice. `BGSStoryManager.cpp` swaps those two calls (checked
  as calls to 29871 before patching) and passes the judged character while `ProcessEvent` runs one of its events.
