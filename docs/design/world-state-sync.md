# World state sync

Status: **steps 1 to 3 done** (2026-10-19, 2026-10-20): what drifts between two games and how often the
engine records changes are in `docs/research/world-state-drift.md`; a form's save data round-trips through
our own buffers in a running game (`Games/FormState.cpp`, `docs/re/systems/form-state.md`). **Step 4 is
next.** Rewritten 2026-10-20: save data goes to a guest only at join and host reload (save transfer); while
playing, vanilla state goes as plain messages, and save data per form is kept for mods, later. AI state is
not part of this sync: the owner sends it as its own record (`docs/design/copy-contract.md`, "AI state").
Designed 2026-10-12.

## The problem

Every game runs the world on its own, so the games drift apart: a lever fires on both games, each game starts
its own radiant quests and rolls its own alias fills, leveled containers re-roll per game, and mods change
their quests, globals and objects on each game separately. The copy contract keeps *actors* consistent;
this keeps the rest of the world the host's.

## The idea

```
 JOIN or HOST RELOAD          WHILE PLAYING (vanilla)            LATER (mods)
 ───────────────────          ───────────────────────            ────────────
 host saves, sends the        the host's game changes        ->  a mod's form changes on the
 file, the guest loads it     something: a plain message         host -> that form's save
 (save transfer,              "quest X at stage 20",             data (FormState)
  join-flow.md)               "global Y = 3",
                              "chest Z holds ..."

                guests never change the world themselves (below, "Only the host's game decides"):
                a guest's levers, triggers and kills go to the host, the result comes back
```

```
// host: the engine's change funnel tells what changed (14607, below)
on change recorded(form, flags):
    dirty.add(form)

// host, a few times a second
for form in dirty:
    if a message covers it:          send(message(form))            // quest stage, global, lock, container
    else if a mod's form (later):    send(FormState::Save(form))
dirty.clear()

// guest
on message:     apply with the engine's own call                   // SetStage, a global's value, Lock
on save data:   FormState::Load(form, data)                        // mods, later
```

The rule: **the world comes from the host; each player's character stays their own** (the host stores every
player's character too, `docs/design/join-flow.md` stage 5).

Why not save data for everything while playing (the 2026-10-12 design): a message is applied with the
engine's own call, which runs that change's side effects the normal way, is small, and can be checked by a
scenario field by field. Save data loaded into a live game skips those and needs each form type tested live.
It stays for what no message can know: a mod's forms.

## What a save contains

What save transfer sends, and what `FormState` reads and writes per form. Layout from
[UESP: Skyrim save file format](https://en.uesp.net/wiki/Skyrim_Mod:Save_File_Format):

```
Header, screenshot, plugin list
Global data table 1     misc stats, player location, TES, global variables, created objects, effects,
                        weather, audio, sky cells
Global data table 2     process lists, combat, interface, actor causes, detection, location metadata,
                        quest static data, story teller, magic favourites, player controls, story event
                        manager, ingredient shared, menu controls, menu topic manager
Change forms            per changed form: form id, change flags, type, version, its SaveGame bytes
Global data table 3     temp effects, Papyrus (every script instance's variables, stacks, registrations),
                        anim objects, timer, synchronized animations, main
Form id array, visited worldspaces
(+ the SKSE co-save .skse, written by each SKSE plugin)
```

Loading resets to plugin data, loads the global tables, calls each change form's `LoadGame` with its
bytes, loads Papyrus, then SKSE plugins load their co-save parts.

## What changes, and what covers it

| State | Where | Covered by |
|---|---|---|
| Vanilla state changed at runtime: quest stages, objectives and alias fills, globals, references' lock/open/enabled/owner, containers | change forms, the global table | at join the save; while playing a message per kind (steps 4, 6) |
| Records added by a plugin | the plugin | nothing to sync if both games run the same mods (ModPolicy) |
| A mod's forms changed at runtime | change forms | at join the save; while playing their save data, later |
| Script variables | Papyrus section | at join the save; while playing the host runs world scripts, so their effects show up as the changes above |
| SKSE plugin memory / co-save | the plugin | at join the co-save, case by case |
| Actors | `ACHR` | the copy contract, not this |

## Finding what changed on the host

Every change goes through one engine funnel, `BGSSaveLoadGame::AddChangeFlags` (offset 0x604320 in 1.6.1170,
our name, no Address Library ID in our data; named in IDA). It is called by `TESForm::AddChange` (14607, vfunc
0x0A) and every override, from vanilla code, Papyrus and SKSE plugins alike. It records (form id, change flags)
for the next save. The host hooks it only to learn *what* changed; what is sent is the message (or, for a
mod's form, the save data).

- It ignores changes while a load runs (TLS +0x670 bit 0, or the manager's +0x340 bit 0), so a guest
  applying save data through `LoadGame` records nothing and nothing echoes back. A guest applying a message
  makes the engine's ordinary change; the guest records it in its own save, which a guest never writes while
  connected (`join-flow.md`).
- It skips ignored forms (form flags 0x4000) and forms without an id. Its 4th argument forces it: 14608 is
  `AddChange` with force set, called during loads (`PromoteReference`, `FinishLoadGame`, `Revert`), so a
  forced change is recorded even while a load runs.
- **Global variables never reach it**: they are saved whole as one table, so the host diffs that table.
- The flags say *what* changed, per form type ([UESP: change flags](https://en.uesp.net/wiki/Skyrim_Mod:ChangeFlags)).
- It runs on worker threads: the dirty set must be lock-free.

No periodic compare-and-resend: a guest's world drifting from the host's is a bug a scenario should catch.
The world desync check (step 7) compares the two games in the scenarios.

### As built (2026-10-21)

```
host game
  TESForm::AddChange (14607), forced (14608),      hooks: Games/FormChanges
  the change map's remove (35511: RemoveChange 14609,
    a state back to its default; a demotion on a wait)
  + a non-actor's repeat moves, marked again       (TESObjectREFR::RecordMove)
    -> watchers, on the engine's threads (fixed slots)
       HostEngine's: a REFR (not created, not forced), recorded, every flag
         -> changed refs (a leaf lock: nothing taken under it)
       the harness's ChangeLog: counts for `change-rate`
  HostWorldState, each update: HostEngine::TakeObjectChanges
    1. state, read now, per changed ref:
         a row's flag    -> its value, applied with the engine's call
         any other flag  -> its save data (kSaveData, FormState)
         not sent        -> game-only extras, promoted, cell changed
    2. then the events in order: doors a script activated, script animations
guest: GuestEngine::ApplyObjectChange; save data loaded, then its flags marked

guest: a cell loads (TESCellFullyLoadedEvent; each loaded cell on joining)
  -> CellStateRequest {cell, [ref, row flags its record holds]}
host HostWorldState: cell attached here? no -> kept, retried each update
  HostEngine::ReadCellState: the host's created refs (kCreate with an item's count and
    extra data, rows; dropped-items.md); each
    ref with row changes in either game -> its rows and kRecord (the rows
    the host's record holds: the guest's is made the same); then each
    container: rolled here (16038), kContents
    -> NotifyObjectChanges {Cell} to that guest
guest: rolls no world container's leveled entries (16129 skipped)
guest: a state already as the host's is skipped (a load raises no events)
```

Rows (2026-10-21, `Games/ReferenceStates.cpp`):

| Flag | Kind | Apply (the engine's call) |
|---|---|---|
| form flags `0x1` | `kEnabled` | `EnableImpl` / `DisableImpl` (19800, 19801) |
| lock `0x1000` | `kLock` | `SetLockState`: the lock, `AddLockChange` (19512) |
| open state `0x800000` | `kOpenState` | `BGSOpenCloseForm::SetOpenState` (14287): the swing, its events |
| move `0x2` | `kMove` | `SetPositionOnAxis` (22053), `SetRotation` (22054) per axis, then the 3D's update |

| scale `0x10` | `kScale` | `SetScale` (19665): the 3D and collision follow |
| owner `0x40` | `kOwner` | `SetOwner` (20198), the owner as a `GameId` |
| base object `0x80` | `kBase` | `SetObjectReference` (19785), then the 3D again: a placed leveled item's pick (a guest picks none, 20232 skipped) |
| animation `0x10000000` | `kGraph` | the graphs' save data loaded (`AnimationGraphState`), with a cell's state only: live, the scripts' animations are relayed as events |
| every other flag | `kSaveData` | the last row: `FormState::Load`, then the flags marked. **Off** (`cSaveDataFallback`) |

Containers' items go as `kItems` events (the container event), so inventory `0x20` is not a row. Not sent:
game-only extras, promoted, cell changed (`cNotSent`, below).

**The save-data row is off while vanilla's states get rows (decided 2026-10-21).** With it on, a state with no row
still matched as data, so a missing side effect showed only where a scenario looked: the container event never
fired on the guest because the inventory's save data had already set the count. Off, a flag with no row is not
sent and logged once (`Reference sync: change flags ... have no row`); `coop_test.py unsynced` lists them, the
rows still to write. After the object scenarios: havok moved `0x4`, activating children `0x4000000`, animation
`0x10000000`; after the travelling ones also base object `0x80` and encounter zone `0x20000000` (a cell's first
load). It is switched on for mods' states. `ReferenceStates::Read` walks the rows in order, each taking its flags, the last what is left;
`Apply` finds the row by kind. Both engines only call these two.

**State at a cell load (2026-10-21).** A cell's references stay in the engine's memory once loaded, so live
changes reach a guest's references in cells it loaded before, unloaded or not; what it missed is a cell it never
loaded and changes made while it was away. Each cell it loads asks the host (`CellStateRequest`, with the
references its own game changed, so its own stale changes go back too), and every loaded cell on joining (no
load event comes for those). The host answers once its game has that cell attached. The answer is applied as a
load: a state already as the host's is left alone, so no events. Live changes are always applied, raising what
the host's game raised (opening a door re-sets its lock: `lock_changed` on both).

- Enable and Disable (19800, 19801) both flip the disabled flag through `TESForm::SetDisabled` (14646), which
  calls `AddChange(1)`; `AddLockChange` (19512) calls `AddChange(0x1000)`. The funnel returns 1 whenever it
  records, the flag already set or not, and 0 while a load runs, so a cell loading sends nothing.
- State before events: a script enabling an object and then playing its animation finds it enabled on the
  guest. Two changes in one update send the state as it is then.
- Events (a door a script opens, a script's animation) have no change flag worth reading: they keep their
  hooks. So does a guest's lock change: a guest's own action, proposed to the host (`LockChangeEvent`).
- Adding a state: a row in `Games/ReferenceStates.cpp` above the save-data row (kind, change flags, read, apply).
  A row fills `ObjectChange::Value`, or `Data` for a kind listed in `ObjectChange::HasData` (a move's six floats);
  its read returns false when there is nothing to send.
- **Rows where the oracle shows a side effect, save data for the rest (2026-10-21).** Tried both ways with
  `world-changes-reach-guest` (step 7). Save data alone gets the live state right for most flags and the 3D for
  enable and disable (`FinishLoadGame` redoes them), but a door set open does not swing, the guest's scripts
  get no events, and a load records no change, so the guest's cell unloading lost it (now marked after the
  load). Each kind with a missing side effect got a row; scale and owner stay save data and match.
- **What the funnel alone misses**, each found by the oracle (`docs/re/systems/form-state.md`, "Change records"):
  a loaded reference's move is recorded once (its 3D's moved bit), so later moves are marked again in the
  position and angle hooks; a state back to its default removes its flag (`RemoveChange`), so that is watched
  too; promoted and cell changed are the location header's, which `FormState` does not carry (loaded alone they
  left a half-promoted shrine on the guest); game-only extras re-run alias data under the quests' locks.
- **Engine calls for vanilla, save data for the rest (decided 2026-10-21).** A state is applied with the
  engine's own call because nearly every vanilla one needs its side effects: Enable/Disable load or remove the
  3D, a door plays its swing, a container updates its inventory. Sending save data for every flag
  (`FormState::Save`/`Load`, what the 2026-10-12 design did) would make most vanilla states exceptions, and
  `Load` refuses while a save or load runs, so changes would need a retry queue. Later, for mods, the same
  loop gains a second branch: a changed form or flag with no row goes as its save data. Revisit the default if
  rows start appearing whose engine call does nothing `Load` would not (locks are the one so far: their save
  data round-trips, `form-state-round-trips`). Entry: `docs/ideas.md`, "Mods' world state through the save
  system".

**Physics objects' rest (2026-10-21, live physics since 2026-10-24).** The host's game simulates every body and the
guests show its flight (`docs/design/physics-sync.md`); where a body rests still goes out as its move row, so a save
and a cell load keep it.

```
host: PhysicsBodies (Bridges/)
  funnel's havok move 0x4 on a REFR (or an actor's ragdoll) -> awake
  each update: its bodies' frames to the guests (HostPhysics); once none is awake (the island's mark), it rests:
    the bodies' rests (NotifyBodyRests) and, away from where its physics took it (a settle in place, as every body
    at a cell load, is none), its move row (HostEngine::TakeObjectChanges)
guest: the host's frames hold it keyframed and move it; the rest and the move row put it there (zero velocity,
       keyframed: 78089, 78090, 77866), then simulated again, asleep (78182, 60850), for its player to push
```

A physics move counts as a move in a reference's record (`ReferenceStates::RecordedRows`): a save keeps its
position either way. Before live physics (2026-10-21) each game simulated the flight itself and the host's rest was
found by watching positions (`PhysicsRests`: within 0.5 units for 15 updates); the island's mark replaced that. A
guest's own push goes to the host as its impulse since 2026-10-24 (physics sync's step 5).

Excluded: what is already synced live (actor positions, actor values, equipment), actors (`ACHR`), and the
clock's globals (`HostClock`: hour, date, days passed).

Order from the measurements (`docs/research/world-state-drift.md`): global variables, quest
stages/objectives/flags with their alias fills and the story-manager nodes, then references' lock, open,
ownership, enabled state, and containers. Leveled containers re-roll when their cell loads on each game: a
guest takes the host's contents as the cell loads.

## Only the host's game decides (decided 2026-10-19)

Every game runs its own story manager and start-game quests and rolls its own alias fills, so quests
differ before anyone plays (9 to 13 on a settled duo, all alias fills; the decoded change form is in
`docs/re/systems/quest-change-form.md`), and a guest would keep starting its own radiant quests and world
encounters after joining. Decided: **only the host's game starts quests, fills aliases and changes world
state.** The guest's story manager and quest starts are off, as copies run no AI; its quests, with their
stages, objectives and alias fills, come from the host.

```
 guest pulls a lever ──► the guest's game does not run its world script
                         └──► "lever pulled" ──► the host's game runs it
                                                 └──► the result ──► every game
```

The rule that follows: **a guest never decides world state; it reports what its player did, and the host's
game reacts as if it happened there.** The scripts a guest still runs itself (its body's and items', the Elder
Scroll's reading) send their world calls to the host instead (a quest's stage, 2026-10-23: `docs/design/cutscenes.md`,
"A guest's own script changing the world"). Everyone else then gets the result from the host. What a guest's
player does that the world reacts to, and where each stands:

| A guest's player... | On the host today | Still needed |
|---|---|---|
| kills an NPC (the host owns every NPC) | the host applies the damage; the NPC dies there, with kill credit (`death-host-npc`, `client-*-kill-credit`) | the killer is the guest's copy: scripts and story-manager conditions asking "killed by the player" (`OnDeath`, kill events) see an NPC |
| activates something: lever, button, door, chest, shrine | relayed: the host's copy of the guest activates it (`ActivateRequest` / `NotifyActivate`, `ObjectService`); scenarios for doors only | `OnActivate` scripts testing `akActionRef == Game.GetPlayer()`: the party model's rules 1-2 on the host; the guest's own run of the script must change nothing |
| walks into a trigger volume (`OnTriggerEnter`: traps, ambushes, scene starts) | not checked: whether the copy, placed by movement sync, fires the host's trigger | the same player checks; the guest's local trigger must change nothing |
| enters a location (world encounters, "location cleared", story manager change-location events) | nothing: the engine raises these for the host's own player only | the guest's arrivals raised on the host, for the guest's character (the party tether makes it rare, not impossible) |
| picks up, drops or uses a quest item (`OnContainerChanged`, `OnItemAdded`, alias inventories) | nothing: each player's inventory is their own | the host learns of it, so its quest scripts see it |
| reads a book or note that starts a quest, crafts, raises a skill, commits a crime, picks a lock or a pocket | not checked (lock state is synced) | each one an event the host must see |
| chooses a dialogue line | shared dialogue, parked: `docs/progress.md`, "Shared dialogue: sync the step, not the list" | the step sync |

Each row gets a scenario: the guest does it, and the host's quest, script or world state reacts, then
the guest's from the host.

How (decided 2026-10-19, `docs/design/party-model.md`, "A guest's actions on the host"): the guest's
copy acts on the host, and the host's script engine treats every party member's copy as the player, with
"the player" being the member whose action started the script. No per-script or per-mod patches.

**Required: the guest's world scripts stop changing the world.** Today the guest's game runs the world's
scripts for its own player too, so a lever, a trigger or a quest script fires on both games and its effects
happen twice (spike 1 counted 80 to 540 world-script events per scenario window on the guest). Under "only
the host decides", the guest's game keeps raising its player's events (the host needs them) but its world
scripts must not commit world changes; the host's results reach it as messages. How (not run them, run
them and discard their changes, or something between) is step 5.

**Decided 2026-10-21: a guest runs only the scripts about its own player's body and its UI**
(`docs/research/guest-scripts-and-mods.md`). Every Papyrus event passes `VirtualMachine::SendEvent` (104800),
and its handle names its subject (53508): a guest delivers it when the subject is its own player (the
player reference, an active effect on it), or when it is the game's own UI: SKSE's events (input, menus,
mod events) and `OnPlayerLoadGame`. Everything else (references, NPCs, quests, aliases, items) runs on the
host only, and its results reach the guest as state. Rejected: a list of event names (mods' own events
would pass) and running scripts while discarding their world changes (every native of every SKSE plugin
would have to be known). Cost: a quest that tracks one player's body (Frostfall, needs mods) tracks the
host's player only; per-player system quests are an idea for mod support (`docs/ideas.md`). A quest's alias
fill of a reference the guest never loaded is loaded the engine's way (its promote task, 15319), as the
host's game did.

**A guest's own quests** (built 2026-10-23, the first piece of per-player quests): a quest the guest's own scripts
start is the guest's. `TESQuest::EnsureQuestStarted` (25003, every start: `Quest.Start`'s and the engine's) is hooked
on a guest; a start in a stack its own player caused (GuestScripts' cause 0x14) of a quest not running yet records the
quest (`PlayerNatives::IsOwnQuest`). Its stages stay on the guest (`PlayerNatives::SendToHost` does not send them),
and its events, the quest's handle or an alias's (53508: an alias's handle is the quest's form id with the alias id
above it, top word 0), are delivered (`WorldEvents::IsOwnEvent`). Story quests the host runs (`MQ206`) are not
started by the guest's scripts and still go to the host. Found when the beast form broke: its later stages went to
the host, which changed the guest's character there (`docs/re/crashes/2026-10-23-guest-beast-form-graph-gc.md`).
Not covered: a quest that starts itself (start game enabled, the story manager), and a host's copy of the same quest
overwriting the guest's (quest or global sync).

Its first slice (2026-10-21): a guest's
game does not deliver activation and trigger events to the world's scripts; the host's copy of the guest
raises them there, and the results come back (below, step 5). Other events (hits, deaths, container changes,
timers, loads) still run on both games.

```
guest pulls a lever  → ActivateRequest (exists) ─────► host: the copy activates it
guest's VM: OnActivate on the lever → dropped          host's script runs (rules 1-2)
                                                         ├ door it opens   → relayed (by reference)
                                                         ├ its animations  → relayed
                                                         ├ spell on the copy → to its owner
                                                         └ items on the copy → inventory changes
```

## Unknowns

- **Messages and the engine's side effects.** Each kind's apply call must be checked for what it does
  besides the change (`SetStage` runs the stage's fragment: on a guest that is a world script, which step 5
  stops; a quest's start fills aliases, which must be the host's fills).
- **Save data in a live game (mods, later).** The engine loads a reference's stored changes into the live game
  itself when its cell attaches (35569), and `FormState::Load` follows it. Each form type still has to be
  tested live; a door's lock is. Still open: the reference location header (save-local cell and worldspace
  indices, so a moved reference's position does not travel) and ids of forms created during play
  (0xFF......), which differ per game.
- **Size and rate.** Some forms change constantly: the rates are in `world-state-drift.md`.
- **Save transfer specifics** (`join-flow.md`): our session surviving a guest loading a save while connected,
  how the guest's own character is carried across, time and file size of a modded save.
- **Prior art:** none known for Skyrim. SkyMP runs mod scripts on its server in its own Papyrus VM
  (`reference/plugins/skymp/papyrus-vm`); our host is a real Skyrim and runs them natively.

## Steps

```
 1  save diff tool                                    done
 2  watch the change funnel                           done
 3  FormState (copies' appearance now; mods later)    done
 4  globals + quest stages as messages                built, loose ends open
 5  guests stop changing the world  (the double sword)
 6  locks, doors, containers as messages
 7  world desync check in the scenarios
 -  join / host reload: save transfer                 join-flow.md
 -  mods: save data for forms no message covers       later
```

1. **Save diff tool.** Done 2026-10-19: `scripts/re/save_diff.py`, `coop_test.py save-diff`. Not yet run with
   mods installed.
2. **Observe the funnel.** Done 2026-10-19: a harness-only hook on 14607/14608
   (`Code/client/TestHarness/ChangeLog.cpp`), `coop_test.py change-rate`. Findings:
   `docs/research/world-state-drift.md`.
3. **Buffers.** Done 2026-10-20: `FormState::Save`/`Load` (`Games/FormState.cpp`), scenario
   `form-state-round-trips` (a door's lock; the copies' appearance, now on `FormState`, which fixed upstream's
   guessed buffer layouts).
4. **Globals and quests as messages:** the global table diffed, quest stages, objectives, flags and alias
   fills from the host, each with a scenario. Before it, 2026-10-20: `coop_test.py quest-walk` walks real
   quests from a late save (main, side, radiant) and shows what each game does without sync
   (`docs/research/world-state-drift.md`, "Quest walks"); the same walks, every step matching, are step 4's
   check. Globals done 2026-10-20: `HostWorldState` compares the host's table every 250 ms (the bridge leaves out
   constants and marks the calendar's globals, which `HostWorldState` leaves to `HostClock`) and sends what changed (`NotifyGlobals`), a joining
   player gets all of it; a guest sets each as Papyrus' `SetValue` does (55923: the value, no change record,
   never a constant). Bridge cases `world_state_tests.cpp`, scenarios `global-reaches-guest`,
   `late-join-gets-globals`. Quests built 2026-10-20: `HostWorldState` compares a snapshot of every quest
   twice a second (flags running/starting/completed/failed, stage, done stages, objectives, reference alias
   fills) and sends the changed ones whole (`NotifyQuests`); the first look and a joining player get all of
   them as a load (`Join`). Snapshots rather than the change funnel: a script's alias fill records no change
   (`docs/re/systems/quest-apply.md`). A guest applies with the engine's calls: the running flag directly
   (no start of its own, no rolls), `ForceRefIntoAlias`, `SetStage` with each stage's fragment skipped,
   `BGSQuestObjective::SetState`, `SetCompleted`/`SetFailed`; at join the fields only, as a load does.
   Scenarios `quest-stage-reaches-guest` (the harness counts fragments: none on the guest),
   `quest-completes-for-guest`, `quest-roles-match-host`, `late-join-gets-quests`; the main-quest walk
   matches at every step (14 of 14, before: 1). A guest's story manager starts no quest (2026-10-20, `docs/re/systems/story-manager.md`,
   `guest-story-starts-no-quest`). Still open, in `docs/progress.md`: fills of references not in the guest's
   memory, location aliases, created references, quests a guest's scripts start (step 5), deleting upstream's
   `QuestService`.
5. **The guest's world scripts stop changing the world** (above, "Only the host's game decides"): a scenario
   where the guest pulls a lever and its effect happens once, from the host. Slice 1 built 2026-10-21:
   activation and trigger events (`Papyrus/WorldEvents.cpp`); a script's doors and animations come from the
   host (`HostWorldState`, `NotifyObjectChanges`); the host's world's spells on a guest's
   character go to its owner (`MagicTarget.cpp`, `GuestScripts`' `CastFunctor` tag). Scenarios
   `sword-box-asks-client` (the sword goes once, comes back once), `statue-fires-at-client`,
   `ring-turns-for-client`, `shrine-blesses-client`. Engine notes `docs/re/systems/guest-world-events.md`.
   Next: the other events a player's action raises on the world (hits, deaths, containers, reading),
   then the guest's timers and loads.
6. **References and containers as messages:** lock, open, enabled and owner state (doors, locks and activation
   are bridge relays already, phase 4), containers' contents, leveled containers as a guest's cell loads.
   Enabled state and locks built 2026-10-21: the host reads each reference's state from the change funnel
   (`FormChanges`, one row per state in `ReferenceStates`, "As built") and sends it with its object changes
   (`NotifyObjectChanges`); `sword-box-asks-client` checks the box and the sword in the stone swap. A guest's own
   lock change (its lockpick, a key: never its loads or scripts) is a proposal the host's game takes
   (`lock-reaches-others`, `guest-loads-propose-no-locks`). Containers slice 1 built 2026-10-21: a container's change
   travels as its count of the item now (`kItems`, an event kind: item and count), the host's to everyone, a guest's
   as a proposal the host's game takes (`ContainerChangeRequest`); every add and remove is seen through the engine's
   `TESContainerChangedEvent` (`chest-take-reaches-others`, `docs/re/systems/guest-world-events.md`). Per base item,
   with the stacks that carry extra data (enchantment, tempering, a name) since 2026-10-26 (`ObjectChange::Stacks`,
   `docs/design/dropped-items.md`, "Chests"). Slice 2 (2026-10-21): a container's contents are the host's
   roll. Answering a guest's cell state, the host's game rolls each container there as opening it would
   (`InitInventoryIfRequired` 16038) and sends all it holds (`kContents`: item and count each); the guest makes its
   container hold exactly that, raising nothing. While a guest, a game rolls no leveled entries of a world
   container (`InitLeveledItems` 16129 skipped for a non-actor's changes, with the story manager's switch); an
   actor's inventory still rolls. Scenario `guest-opens-hosts-roll` (the client opens the inn's richest leveled
   container: its own roll without this).
7. **World desync check:** compare what each game shows of the world, as `coop_test.py desync` does for actors.
   References built 2026-10-21: the harness's `world_state` reads every reference in the loaded cells
   (base, cell, form flags, position and rotation, scale, its 3D shown or hidden and where, lock, open state,
   owner, items), and `coop_test.py world-diff` compares the two games (created references pair up by base
   and position; actors are `desync`'s). Scenario `world-changes-reach-guest` makes one change of each kind
   on the host with the console, puts it back, and lists every kind the guest's world does not follow.
   First run: disable, enable, lock and items follow; move, rotate, scale, open (a script's `SetOpen`), owner
   and created references do not. Not covered yet: havok moves, harvesting, destruction (QASmoke has no
   flora or destructibles). Since 2026-10-21 `scenario` runs the compare after every scenario (`check_world`:
   the cells both games have loaded, differences lasting 3s, again at home after the reset); a node's pose counts
   only on references a game changed or with a behavior graph (a model's own loops stop off-camera, each game at
   its own phase). Next: quest stages and globals.
   Side effects, the same day: `world_state` with `details` also reads each reference's change record
   (`BGSSaveLoadGame::GetChange` 35577: what survives its cell unloading), its 3D's named nodes (any animation's
   result), its physics bodies (in the world, where), its active behavior graph's variables and the effects on
   it; the scenario compares the object events each game raised (EngineEvents: activate, lock, open/close,
   attach/detach, load/unload, container). Nodes the host moves between two samples (idle loops) are skipped.
   Save data for everything (the rows off, 2026-10-21, before the rows below were written): disable and enable match in every side effect. Lock,
   scale, owner and items match live, but the guest records no change (a load's changes are not recorded), so
   its cell unloading loses them, and raises no `OnLockStateChanged` or `OnItemAdded`. A script's `SetOpen`
   sets the guest's open state but its door does not swing (nodes and physics closed) and no `OnOpen`.

Join and host reload: save transfer, built with the join flow (`join-flow.md`, stages 4 to 6). Mods: save
data for the forms no message covers, once mods are taken on.

## Related

- `docs/design/copy-contract.md`: actors (copies) and their AI state are the copy contract's, not this.
- `docs/design/join-flow.md`: save transfer at join.
- `Code/client/Games/Skyrim/Havok/AnimationGraphState.cpp`: the same save-data technique for animation graphs.
- Scripts checking `== Game.GetPlayer()`: handled on the host by the party model's rules
  (`docs/design/party-model.md`, "A guest's actions on the host"); upstream's per-script patches are gone.
