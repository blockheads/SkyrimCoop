# World state sync through the save system (parked)

Status: **design idea, parked 2026-10-12.** Nothing is built. To be picked up as its own project.

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
   - It skips ignored forms (form flags 0x4000) and forms without an id. Its 4th argument forces it.
   - The flags say *what* changed, per form type ([UESP: change flags](https://en.uesp.net/wiki/Skyrim_Mod:ChangeFlags)).
   - Host: hook the funnel, add (form, flags) to a dirty set (it runs on worker threads: lock-free),
     a few times a second serialize each dirty form with its own `SaveGame` and send form, flags, bytes.
2. **Compare (thorough).** Every so often both games serialize their blocks, hash them, and compare;
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

Excluded: what is already synced live (actor positions, actor values, equipment), and noise the
calendar sync covers (the clock's globals).

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
- **Prior art:** none known for Skyrim. SkyMP runs mod scripts on its server in its own Papyrus VM
  (`reference/plugins/skymp/papyrus-vm`); our host is a real Skyrim and runs them natively.

## Steps when this is picked up

1. **Save diff tool (no sync code, low risk).** In a scenario, save on host and client at the same
   moment; a Python tool parses both `.ess` files and compares them block by block. Gives the real list
   of what differs today, with and without mods, in priority order.
2. **Observe the funnel.** A harness-only hook on `AddChangeFlags` logs (form type, flags, rate) during
   scenarios and normal play with a few popular mods: what the 90% is.
3. **Buffers.** Our own save/load form buffers; round-trip one form on one game.
4. **First live types** (globals, quest stages), each with a scenario; the rest at load screens.
5. **Compare sweep** (hash per block, resend mismatches).
6. **Full sync** for join and manual sync.

## Related

- `docs/design/copy-contract.md`: actors (copies) are handled by the copy contract, not by this.
- `Code/client/Games/Skyrim/Havok/AnimationGraphState.cpp`: the same technique for animation graphs.
- Mod patches that this does not cover (scripts checking `== Game.GetPlayer()`) use
  `SkyrimTogetherUtils.IsPlayer` in `SkyrimTogether.esp` (copy-contract.md, "deliberate exceptions").
