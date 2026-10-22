# A guest's scripts, and what mods need (research 2026-10-21)

The question: which Papyrus scripts may a guest's game run, so that world changes happen once (on the host)
and mods keep working? And the related one: how a guest fills a quest alias with a reference its game has
never loaded. Decisions go in `docs/design/world-state-sync.md` ("Only the host's game decides") and
`docs/design/party-model.md`.

## Two real cases today

- **`CritterSpawn`** (butterflies, dragonflies, fish): `OnLoad` loops on `utility.wait` until the player is
  near, then `spawnInitialCritterBatch()` places each critter with `PlaceAtMe`. Both games spawn a swarm; the
  host's critters also reach the guest as created objects (`kCreate`), so the guest shows two.
- **`defaultSetStageOnDeath`** (on many bosses): `OnDeath` → `myQST.setStage(stageToSet)`. When the host's NPC
  dies, the guest's copy dies too and its script sets the stage with the fragment on the guest, while the
  host sets the same stage (fragment and all) and sends it: the fragment's effects happen twice.

## Precedents

- **SkyMP** (dedicated server, reimplemented game logic): the client hooks the VM's `SendEvent` (the same
  function as our `WorldEvents`, 104800) and blanks the event name of **every** event except `OnUpdate`,
  unless a script on the target is named `SKI_*` (SkyUI) or `defaultDisableHavokOnLoad`
  (`skyrim-platform/src/platform_se/skyrim_platform/FridaHooks.cpp`, `OnSendEventEnter`;
  `skymp5-client/src/services/services/blockPapyrusEventsService.ts`). Its server runs a small Papyrus VM
  with ~20 natives; vanilla quests do not run. Mods beyond the allowlist do nothing on clients.
  <https://github.com/skyrim-multiplayer/skymp>
- **Skyrim Together Reborn** (upstream): every client runs every script; the party leader's quest stages
  are pushed to the others. Effects happen on every game.

## The engine

- **Every Papyrus event goes through `VirtualMachine::SendEvent` (104800)**: engine events, `OnUpdate` and
  `OnUpdateGameTime` (the update registrations), and SKSE's (`OnKeyDown`, `OnMenuOpen`, mod events; SKSE
  calls the VM's `SendEvent`). One hook sees them all, with the handle, the event name and its arguments.
- **A handle says whose script it is** (`SkyrimScript::HandlePolicy::GetHandleForObject`, 53508):

  | Bits 48-63 | Bits 32-47 | Low 32 bits | Object |
  |---|---|---|---|
  | `0x0000` | `0xFFFF` | its form id | a form: reference, actor, quest, … |
  | `0x0000` | the alias id | the quest's form id | a quest alias (VM types 139-141) |
  | `0x0002` | the effect's unique id | the actor it is on | an active magic effect (type 142) |
  | `0x0001` | the extra data's unique id | the container | an item in a container |

  So a hook can tell "a script on my player's body" from "a script on the world" with no script names.
- **What does not go through `SendEvent`**: fragments (quest stage, scene, topic info, package, perk) are
  method calls; stacks suspended in a save resume on load (a guest loads the host's save at join); an SKSE
  plugin can call scripts directly. A guest's world-script stack can also start from these.
- **Natives** all pass `NativeFunctionBase::Call` (104651) with the function's name, class and the running
  stack. Usable as a backstop (a world-changing native from a stack whose subject is the world), but SKSE
  plugins add natives we cannot classify.

## Mods

Read from source:

- **Campfire / Frostfall** (chesko256/Campfire, 398 scripts: 51 quest, 53 alias, 78 object, 60 magic
  effect). Player-centric logic lives in **quest** scripts: `_Frost_ExposureSystem` (a quest) polls
  `Game.GetPlayer()` on `RegisterForSingleUpdate` and keeps the player's exposure, warmth and wetness in
  **GlobalVariables** (`_Frost_ExposureLevel`, `_Frost_AttributeWarmth`, `_Frost_WetLevel`, …). Our global
  sync sends every host global to the guests, so today a guest's Frostfall meter would show the host's
  exposure. Placing a tent: the dropped item's reference runs `OnInit` → `RegisterForSingleUpdate` →
  `OnUpdate` → `Initialize()` → threaded placement through quest scripts and **mod events**
  (`_Camp_ObjectPlacementThread*`, `PlaceAtMe` of each part). 191 `PlaceAtMe`, 230
  `RegisterForSingleUpdate`, 71 `RegisterForModEvent`.
- **SkyUI** (schlangster/skyui): every script is a **quest** (`SKI_QuestBase extends Quest`) or the
  `SKI_PlayerLoadGameAlias` (`OnPlayerLoadGame` re-registers everything). The MCM runs on menu events
  (`RegisterForMenu`), keys and mod events from the menu (`SKICP_*`); each mod's config page is a quest
  (`SKI_ConfigBase`). The example needs mod in its source (`PN_NeedsManager`) is a quest too.

Categories, and where each lands under the options below:

| Kind | Example | Its scripts are on |
|---|---|---|
| World and quest content | vanilla, Falskaar, Bruma, Legacy's museum displays | references, NPCs, quests, aliases |
| Spawners | `CritterSpawn`, OBIS, Immersive Patrols | references and quests (`OnLoad`, `OnCellAttach`) |
| Player-centric needs | Frostfall, iNeed, SunHelm | quests (polling), globals |
| Player-placed objects | Campfire tents | the dropped item's reference, quests, mod events |
| UI | SkyUI MCM, widgets | quests, menu/key/mod events |
| Body effects | spell packs (`OnEffectStart`) | active magic effects |
| Engine-only | SKSE plugins (no Papyrus) | none: not reached by any event filter |

## The options

1. **By subject (recommended).** On a guest, `SendEvent` delivers an event only when its handle is about
   that game's own player: an active effect on the player (`0x0002`, low bits `0x14`), the player
   reference itself, plus a **UI lane** (menu, key and control events, `OnPlayerLoadGame`, and the mod
   events a menu raises). Every other subject (references, NPCs, quests, aliases, items) runs on the host
   only; its results reach the guest as state (rows, quests, globals, created objects, containers).
   - Works for world/quest content and spawners (once on the host), body effects (the owner's game, as
     the copy contract says), UI.
   - Unknown SKSE natives are never a problem: world scripts do not run on the guest.
   - Player-centric needs mods follow the **host's** player: `Game.GetPlayer()` on the host is the host. A
     guest never gets cold of its own. Consistent with "the party is the Dragonborn"; per-player needs
     would need per-player script instances (not generic).
   - A guest's player-placed objects need the guest's action sent to the host (drop/use an item: a row of
     the party model's table, not built); the host then runs the placement as the guest's copy.
   - Needs a backstop for the paths that skip `SendEvent` (fragments, resumed stacks).
2. **By event name** (extend today's drop list: `OnLoad`, `OnCellAttach`, `OnDeath`, `OnHit`, `OnUpdate`,
   `OnItemAdded`, …). Same as 1 for vanilla, but **mod events pass** (any name): Campfire's placement threads
   and other mods' world logic run on both games. A list to maintain.
3. **Run everything, discard world writes** (block world-changing natives on a guest). Keeps per-player
   needs and local visuals, but: every world-writing native in vanilla, SKSE, PapyrusExtender and each
   mod's SKSE plugin must be known (unknowable); a blocked `PlaceAtMe` returns None and the script runs on
   a wrong assumption; script state diverges between games; per-player globals still collide with the
   host's global sync.

SkyMP's choice is 1 with a name allowlist instead of a subject rule, and `OnUpdate` left open.

## Alias fills of references the guest has not loaded

The host's quest fills an alias with a reference in an unloaded cell (the bounty's bandit, Gadnor): the
engine loads it from the plugin. `QueuedPromoteQuestTask` (25212) collects the location's unique
references (`BGSLocation` 18359) and runs `QueuedPromoteReferencesTask` (15319) → `sub_1401FEDB0`: for each
entry `{reference id, parent cell or worldspace, grid x/y}` it looks the reference up; if it is not in
memory, `sub_1402008C0` seeks its cell's (or worldspace grid's) record group in the plugin
(`TESObjectCELL` 19080, `TESWorldSpace` 20456), finds the record, loads it (`TESDataHandler` `0x1401BBAA0`)
and `BGSDynamicPersistenceManager::PromoteReference` (15330) makes it persistent.

So a guest can load the reference the same way the host's game did, given its parent cell or worldspace
and grid (the host reads them from its promoted reference). Recommended: the host's fill carries them; a
guest missing the reference queues the engine's promote and fills the alias once it is in memory (pending
until then). Creation Club items and the bounty letter are created references (`0xFF`): those map through
the created-object ids (`kCreatedModId`) when they are world objects; ones inside an inventory are not
covered.

## As built (2026-10-21)

Option 1 with a cause, not only a subject (`Papyrus/WorldEvents.cpp`, `Papyrus/GuestScripts.cpp`): a guest's
`SendEvent` delivers an event when SKSE sent it (the caller's return address in SKSE's module, found from its
interface at load: input, menus, mod events), when it is `OnPlayerLoadGame`, when its handle is the player or an
effect on it, or when it answers a timer or registration (`RegisterFor*Update*`, animation events, sleep, LOS,
tracked stats) that a script it ran set. A delivered event's stack is tagged with the player (rule 2's tags), so
what it starts, queues or registers runs too: the guest's own Beast Form starts its `PlayerWerewolfQuest` and its
timers and `SetRace` event run (`client-beast-form-stays-client`). Scenarios: `guest-spawns-no-critters` (fails
without it: the client's twin of the spawner placed its own dragonflies), `death-stage-runs-once` (passes either
way: a copy's death raises no `OnDeath` on a guest). Not tested here: the UI lane (no SKSE script mod in the test
install). Not covered: fragments and stacks resumed from a save (no `SendEvent`).
