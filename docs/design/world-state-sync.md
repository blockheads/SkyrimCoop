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
game reacts as if it happened there.** Everyone else then gets the result from the host. What a guest's
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
them and discard their changes, or something between) is open; it is step 5. Until it is done, a guest's
action in the world can happen twice.

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
   matches at every step (14 of 14, before: 1). Still open, in `docs/progress.md`: fills of references not in
   the guest's memory, location aliases, created references, the guest's own quest starts, deleting
   upstream's `QuestService`.
5. **The guest's world scripts stop changing the world** (above, "Only the host's game decides"): a scenario
   where the guest pulls a lever and its effect happens once, from the host.
6. **References and containers as messages:** lock, open, enabled and owner state (doors, locks and activation
   are bridge relays already, phase 4), containers' contents, leveled containers as a guest's cell loads.
7. **World desync check:** after each scenario, compare quest stages, globals, locks and containers between
   the games, as `coop_test.py desync` does for actors.

Join and host reload: save transfer, built with the join flow (`join-flow.md`, stages 4 to 6). Mods: save
data for the forms no message covers, once mods are taken on.

## Related

- `docs/design/copy-contract.md`: actors (copies) and their AI state are the copy contract's, not this.
- `docs/design/join-flow.md`: save transfer at join.
- `Code/client/Games/Skyrim/Havok/AnimationGraphState.cpp`: the same save-data technique for animation graphs.
- Scripts checking `== Game.GetPlayer()`: handled on the host by the party model's rules
  (`docs/design/party-model.md`, "A guest's actions on the host"); upstream's per-script patches are gone.
