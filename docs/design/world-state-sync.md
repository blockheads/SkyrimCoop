# World state sync through the save system

Status: **steps 1 and 2 done 2026-10-19** (research tooling, no sync code): what drifts between two
games and how often the engine records changes are in `docs/research/world-state-drift.md`. Step 3 is
next. Designed 2026-10-12.

## The problem

Mods change the world on each game independently, so the games drift apart. The copy contract and the
engine-level guards keep *actors* consistent, but a mod's quest stages, globals, containers, enabled
objects and script variables change on each game separately. We cannot know what every mod does, and
should not have to.

## The idea

A Skyrim save already captures every mod's state without knowing what the mod means: each changed form
writes itself (`TESForm::SaveGame`, vfunc 0x0E) and reads itself back (`LoadGame`, 0x0F). Use that
machinery for the host's world:

- **Full sync:** the client gets every block the host's save would write and loads it (at join, on a
  manual "sync now", perhaps at fast travel).
- **Dynamic sync:** the same blocks, but only those that differ between host and client, sent as they
  change.

One code path. The full sync is the dynamic sync run against an empty client. The rule is **the world
comes from the host; each player's character stays their own.** (Replaced 2026-10-13: the host stores
every player's character too, `docs/design/join-flow.md` stage 5; this design then also has to load a
player who has no save of their own.)

## What a save contains

Layout from [UESP: Skyrim save file format](https://en.uesp.net/wiki/Skyrim_Mod:Save_File_Format):

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

## What mods change, and what covers it

| Kind of mod state | Where | Covered by |
|---|---|---|
| Records added by the plugin | the plugin | nothing to sync if both games run the same mods (ModPolicy) |
| Forms changed at runtime (quests, globals, containers, references, factions, NPC data) | change forms | dynamic sync, form type by form type. **The 90%** |
| Script variables | Papyrus section | host runs world scripts, so their effects show up as change forms; diffing per script instance is possible, loading it live is the riskiest part |
| SKSE plugin memory / co-save | the plugin | full sync only, case by case |

## Finding the differences: push plus compare

1. **Push (fast).** Every change goes through one engine funnel, `BGSSaveLoadGame::AddChangeFlags`
   (offset 0x604320 in 1.6.1170, our name, no Address Library ID in our data; named in IDA). It is
   called by `TESForm::AddChange` (14607, vfunc 0x0A) and every override, from vanilla code, Papyrus and
   SKSE plugins alike. It records (form id, change flags) for the next save.
   - It ignores changes while a load runs (TLS +0x670 bit 0, or the manager's +0x340 bit 0), so a
     client applying the host's data through `LoadGame` records nothing and nothing echoes back.
   - It skips ignored forms (form flags 0x4000) and forms without an id. Its 4th argument forces it:
     14608 is `AddChange` with force set, called during loads (`PromoteReference`, `FinishLoadGame`,
     `Revert`), so a forced change is recorded even while a load runs.
   - **Global variables never reach it**: they are saved whole as one table. They need a diff of that
     table instead (step 1-2 findings).
   - The flags say *what* changed, per form type ([UESP: change flags](https://en.uesp.net/wiki/Skyrim_Mod:ChangeFlags)).
   - Host: hook the funnel, add (form, flags) to a dirty set (it runs on worker threads: lock-free),
     a few times a second serialize each dirty form with its own `SaveGame` and send form, flags, bytes.
2. **Compare (thorough).** Every so often both games serialize their blocks, hash them (per parsed
   field: raw bytes carry clock stamps and save-local form id indices), and compare;
   the host resends what differs. That catches what the push missed, including drift the client made
   itself. It doubles as a desync detector.

## Applying on the client

- A form type on the **safe-live list** is loaded at once (`LoadGame`).
- Any other type waits for the client's next load screen, where the destination cell is not live yet.
  That is close to the context `LoadGame` was written for.
- A type is promoted to live once a scenario shows it is safe.

Candidates for the first live types: global variables, quest stages and objectives, factions, a
reference's enabled/locked/owner state, containers in unloaded cells. Deferred at first: anything with
3D or a running script attached.

Excluded: what is already synced live (actor positions, actor values, equipment), actors (`ACHR`:
the copy contract's when loaded, irrelevant while unloaded), and noise the calendar sync covers (the
clock's globals).

Order from the measurements (`docs/research/world-state-drift.md`): global variables (table diff),
quest stages/objectives/flags with the story-manager nodes, then references' lock, open, ownership,
enabled state and containers' inventory in unloaded cells. At load screens only: cells, references with
3D, the destination's leveled inventories, created forms.

## Only the host's game decides (decided 2026-10-19)

Every game runs its own story manager and start-game quests and rolls its own alias fills, so quests
differ before anyone plays (9 to 13 on a settled duo, all alias fills; the decoded change form is in
`docs/re/systems/quest-change-form.md`), and a client keeps starting its own radiant quests and world
encounters after a full sync. Decided: **only the host's game starts quests, fills aliases and changes
world state.** The client's story manager and quest starts are off, as copies run no AI; its quests,
with their stages, objectives and alias fills, come from the host's QUST change forms.

The rule that follows: **a client never decides world state; it reports what its player did, and the
host's game reacts as if it happened there.** Everyone else then gets the result from the host. What a
client's player does that the world reacts to, and where each stands:

| A client's player... | On the host today | Still needed |
|---|---|---|
| kills an NPC (the host owns every NPC) | the host applies the damage; the NPC dies there, with kill credit (`death-host-npc`, `client-*-kill-credit`) | the killer is the client's copy: scripts and story-manager conditions asking "killed by the player" (`OnDeath`, kill events) see an NPC |
| activates something: lever, button, door, chest, shrine | relayed: the host's copy of the client activates it (`ActivateRequest` / `NotifyActivate`, `ObjectService`); scenarios for doors only | `OnActivate` scripts testing `akActionRef == Game.GetPlayer()` reject the copy (patched one by one, copy-contract.md "deliberate exceptions"); the client's own run of the script must change nothing |
| walks into a trigger volume (`OnTriggerEnter`: traps, ambushes, scene starts) | not checked: whether the copy, placed by movement sync, fires the host's trigger | the same player checks; the client's local trigger must change nothing |
| enters a location (world encounters, "location cleared", story manager change-location events) | nothing: the engine raises these for the host's own player only | the client's arrivals raised on the host, for the client's character (the party tether makes it rare, not impossible) |
| picks up, drops or uses a quest item (`OnContainerChanged`, `OnItemAdded`, alias inventories) | nothing: each player's inventory is their own | the host learns of it, so its quest scripts see it |
| reads a book or note that starts a quest, crafts, raises a skill, commits a crime, picks a lock or a pocket | not checked (lock state is synced) | each one an event the host must see |
| chooses a dialogue line | shared dialogue, parked: `docs/progress.md`, "Shared dialogue: sync the step, not the list" | the step sync |

Each row gets a scenario: the client does it, and the host's quest, script or world state reacts, then
the client's from the host.

How (decided 2026-10-19, `docs/design/party-model.md`, "A guest's actions on the host"): the client's
copy acts on the host, and the host's script engine treats every party member's copy as the player, with
"the player" being the member whose action started the script. No per-script or per-mod patches.

**Required: the client's world scripts stop changing the world.** Today the client's game runs the world's
scripts for its own player too, so a lever, a trigger or a quest script fires on both games and its effects
happen twice (spike 1 counted 80 to 540 world-script events per scenario window on the client). Under "only
the host decides", the client's game keeps raising its player's events (the host needs them) but its world
scripts must not commit world changes; the host's results reach it as change forms. How (not run them, run
them and discard their changes, or something between) is open; it is a step below. Until it is done, a
client's action in the world can happen twice.

## Unknowns

- **Loading into a live game.** `LoadGame` normally runs during a save load. Each form type has to be
  tested live. The animation graph state (same kind of trick) worked.
- **The save and load buffers.** `BGSSaveFormBuffer` / `BGSLoadFormBuffer` carry version info and remap
  ids of forms created during play (0xFF......), which differ per game. Mirroring them is the first real
  piece of work, like `AnimationGraphState`'s stream but bigger.
- **Size and rate.** Some forms change constantly: pick types and rates from the observation step.
- **Full sync specifics:** does our session and world survive the client loading a save while
  connected; how the client's own player is carried across (save its player form to a buffer before,
  load it after); time and file size of a modded save.
- **Leveled containers** re-roll per game on cell load: the destination's inventories come from the
  host at the load screen.
- **Prior art:** none known for Skyrim. SkyMP runs mod scripts on its server in its own Papyrus VM
  (`reference/plugins/skymp/papyrus-vm`); our host is a real Skyrim and runs them natively.

## Steps when this is picked up

1. **Save diff tool (no sync code, low risk).** Done 2026-10-19: `scripts/re/save_diff.py`,
   `coop_test.py save-diff`. Not yet run with mods installed.
2. **Observe the funnel.** Done 2026-10-19: a harness-only hook on 14607/14608
   (`Code/client/TestHarness/ChangeLog.cpp`), `coop_test.py change-rate`. Findings:
   `docs/research/world-state-drift.md`.
3. **Buffers.** Our own save/load form buffers; round-trip one form on one game.
4. **First live types** (globals, quest stages), each with a scenario; the rest at load screens.
5. **The client's world scripts stop changing the world** (above, "Only the host's game decides"): a
   scenario where the client pulls a lever and its effect happens once, from the host.
6. **Compare sweep** (hash per block, resend mismatches).
7. **Full sync** for join and manual sync.

## Related

- `docs/design/copy-contract.md`: actors (copies) are handled by the copy contract, not by this.
- `Code/client/Games/Skyrim/Havok/AnimationGraphState.cpp`: the same technique for animation graphs.
- Scripts checking `== Game.GetPlayer()`: handled on the host by the party model's rules
  (`docs/design/party-model.md`, "A guest's actions on the host"); the `SkyrimTogetherUtils.IsPlayer`
  patches in `SkyrimTogether.esp` (copy-contract.md, "deliberate exceptions") predate them.
