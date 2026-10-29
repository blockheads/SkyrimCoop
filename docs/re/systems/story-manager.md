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
  first, then the script event). The host raised a guest's arrival from its copy's until 2026-10-28; the guest's own
  CLOC is sent now, as any other event (below).
- The visitors' parameters: 32429 at +0x19D and 32430 at +0x3F call `ConditionCheckParams`'s constructor (29871:
  params, subject, target, owner form) with `g_thePlayer` twice. `BGSStoryManager.cpp` swaps those two calls (checked
  as calls to 29871 before patching) and passes the judged character while `ProcessEvent` runs one of its events.

## A guest's story events (generic, 2026-10-28)

Every type travels the same way, read from the engine's own type table rather than one raw struct per type:

- `BGSStoryEventManager` (22790): `registeredEvents` at +0, a `BSTArray` of `BGSRegisteredStoryEvent` (0x28: the
  four-character `uniqueID` at +0, its `BSTArray<BGSStoryEventMember>*` at +8). 32330's lookup (0x52B5C0) is
  `registeredEvents[index].members`. A member (0x18) has its type at +0: 0 reference, 1 form, 2 location, 3 integer,
  4 keyword (CommonLibSSE-NG `BGSStoryEventMember::DATA_TYPE`).
- 32330 walks those members over the raw event: a reference or integer takes 4 bytes, any other an 8-byte pointer,
  the raw pointer first moved to its next 8-byte boundary (`test dil, 7`). Each lands in a 64-bit member. So a raw
  event can be rebuilt from the member types alone (`StoryEventRules::Layout`, ClientTests).
- 32330's only caller is `AddEvent_Impl` (0x52BD31, a byte scan of `.text`): hooking it sees each event once, as it
  is queued. `AddEvent_Impl` holds the manager's `eventArrayLock` (+0x60) around it, so the hook only copies the
  members and queues an `OwnStoryEvent`; handles are read on the next update.
- The engine's queued quest start (`TESQuest::EnsureQuestStarted` 25003) is a story event of index 380097: a guest's
  own scripts' starts stay its own, so it is not sent.
- Kills and assaults are raised on both games (`docs/research/player-events.md`, spike 1): on the host with the
  guest's character as the actor, on the guest for its copy of the host's NPC. A guest's event naming any actor
  other than its player is the other game's to raise, and is not sent.

The host raises a guest's event by its type id (the index of the same id in its own table), laid out from the
sent members: the guest's player becomes its character's handle, a reference its handle here, a form its pointer.
One whose members do not fit the type here is dropped with a warning.
