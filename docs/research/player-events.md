# Who is "the player": making a client's actions count on the host

Research, 2026-10-19. Decided before it (`docs/design/world-state-sync.md`, "Only the host's game
decides"): only the host's game starts quests and changes world state, and a client's actions reach the
host. The question here: **how does a client's kill, lever, trigger, item or book count on the host for
everything that reacts to it (the engine, every script, every SKSE plugin), without patching scripts or
mods one by one?** Goal: all of it, and where a design cannot reach all of it, say exactly what is left.

## The problem in one line

The host's game reacts to its own `PlayerCharacter` (form 0x14). A client's player exists on the host only
as a copy, which the engine and every mod see as an NPC. Kills and lever pulls already arrive (the copy
kills the NPC, the copy activates the lever) but nothing that asks "was it the player" says yes.

## Measured: where "the player" is decided

| Layer | What decides | Measured (1.6.1170, vanilla + CC) |
|---|---|---|
| Engine (C++) | code that reads the player singleton `g_thePlayer` (403521) | **2,843 references in 1,286 functions**: kill credit, XP, crime, stats, detection, story events, conditions |
| Story manager | `BGSStoryEventManager::AddEvent_Impl` (32359): the one function every story event goes through | 30 event types (`QuestEvents.h`): `KILL`, `ASSU`, `CLOC`, `SCPT`, `ADIA`, `AHEL`, `AFAV`, `AIPL`, `REMP`, `CRFT`, `LOCK`, `INFC`, `CURE`, `NVPE`, `DEAD`, `SKIL`, `LEVL`, `CHBR`, `INTM`, `BRIB`, `FLAT`, `PRFV`, `PFIN`, `JAIL`, `STIJ`, `ESIA`, `TRES`, `ADCR`, `ARRT`, `CAST`; **68 raise sites in 66 functions** (`Actor::KillImpl`, `PlayerCharacter::PickUpObject`, `DropObject`, `PayFine`, `GoToPrison`, `SetParentCell`, `AddShout`, papyrus `SendStoryEvent`...) |
| Script events | ~50 engine event sources (`ScriptEventSourceHolder`: activate, trigger enter/leave, death, hit, container changed, book read, sell, sleep, lock changed...) feed the script VM; SKSE plugins' events (`RegistrationSet`) and mod events also end in `VirtualMachine::SendEvent` (vfunc 0x24) | one delivery function for engine and plugin events alike |
| Vanilla scripts | 14,301 sources (`Data/Scripts.zip`) | 2,693 name the player, **98% through `Game.GetPlayer()`**, 49 through a `PlayerREF` property; **982 event handlers gate on "is my actor the player"** (`OnTriggerEnter`, `OnActivate`, `OnContainerChanged`, `OnHit`, `OnDeath`...); 134 functions called on the player |
| Mod scripts | Creation Kit practice: an auto-filled `PlayerREF` property, "1000 times faster" than `GetPlayer` | mods identify the player through properties, so a fix at `GetPlayer` alone misses them |
| SKSE plugins (`reference/plugins`) | sinks on engine events, and hooks on call sites deep in engine code, including menus | PapyrusExtender hooks the book menu's read call (50122+0x22D), smithing, tempering, enchanting and alchemy (50477, 50476, 50450, 50449), magic apply (33742), hit events, fast travel; TrueHUD, Precision, po3 Tweaks, SkyrimSouls check the player 10 to 30 times each |

Regex counts over script sources: approximate, but the proportions hold.

What scripts do with the player (vanilla, scripts per function):

- **Effects:** `AddItem` 609, `RemoveItem` 435, `MoveTo` 81, `AddSpell` 81, `AddToFaction` 66,
  `RemoveSpell` 44, `AddPerk` 40, `PlaceAtMe` 33, `PlayIdle` 31, `SetRelationshipRank` 13, `EquipItem` 11...
- **Questions:** `GetItemCount` 164, `GetDistance` 67, `GetCurrentLocation` 49, `IsInCombat` 44,
  `IsInLocation` 39, `GetLevel` 26, `HasLOS` 19, `GetActorValue` 20, `HasSpell` 14...

## Prior art

- **Skyrim Together Reborn:** only the party leader progresses quests; others must not talk to quest NPCs
  or loot quest items ([playguide](https://wiki.tiltedphoques.com/skyrim-together-reborn/guides/playguide)).
  Our fork's inherited `QuestService` relays stage numbers. Upstream also made every script comparison
  of a remote player's copy with the player true (`HookCompareVariables`, 105220, `b87e5820`), globally;
  it was dropped because it broke beast form.
- **Fallout 4 Together:** "only the host's progress counts".
- **skymp:** runs scripts in its own server VM and makes "the player" a property of the script stack:
  `OnActivate`, `OnTrigger*` and `OnHit` record their actor argument for the stack, and `Game.GetPlayer()`
  in that stack returns it (`HeuristicPolicy`, `reference/plugins/skymp`). Scripts it cannot place are
  removed by name. Its server is not Skyrim, so it has no engine code or SKSE plugins to satisfy.

## Why one layer is not enough

| Approach | Gets right | Gets wrong |
|---|---|---|
| **The host's player stands in** (the host activates, kills, enters as its own player) | identity everywhere: engine, story manager, conditions, scripts, SKSE plugins | questions read the host player's state (the client holds the key, the host is asked); effects land on the host (`MoveTo`, furniture, load doors, rewards, messages); a trigger the host's player is not in |
| **The copy counts as the player in scripts** (skymp's per-stack player, upstream's comparison) | questions and effects follow the copy, so the right player, if the copy mirrors the client | only the script layer: the 1,286 engine functions and SKSE plugins test the real singleton |
| **The client's game decides and ships its changes** | everything on the client sees its real player | the client decides what option A rules out (quest starts, random fills), acts on copies of host NPCs, and script variables are not in change forms |

## A design for every layer: decisions at the funnels, consequences through the change funnel

Each action first happens on the acting player's own game, where the engine, every script and every plugin
see the real player: nothing is missed at detection. The client's game then **never commits a decision**.
It hands each decision to the host at the two places where all decisions converge. The host runs it in that
player's context. Plain physical consequences travel as change forms.

### 1. Script decisions: the client's VM delivery

- Capture on the client at `VirtualMachine::SendEvent` (and the VM's own delivery of engine events, to be
  checked in IDA). Engine events, SKSE plugins' events (PapyrusExtender's `OnBookRead`, `OnItemCrafted`...)
  and mod events all arrive there, whatever hook raised them.
- One rule decides where an event runs, by the object it is delivered to:
  - the client's own player, its active magic effects, items in its inventory: **runs on the client**;
  - anything else (a quest, an alias, a reference, an activator, an NPC, all host-owned world): **not run on
    the client**; sent to the host as (form, script, event name, arguments).
- The host delivers it to the same script on the same form, in that player's context (3).
- A consequence: world scripts run only on the host, so their variables (the Papyrus section) exist only
  there. Papyrus state never needs syncing for them.

### 2. Story decisions: the client's story manager

- The client's story manager is off (option A). Its raise sites still run: capture at `AddEvent_Impl`
  (32359), where all 68 sites and every type converge, and send the event (type, data: the engine already
  serializes it generically, as a saved quest's start event shows) to the host.
- The host raises it with the client's player mapped to its copy, in that player's context (3), so story
  nodes pick quests for what the client did: `CLOC` when the client enters a location, `CRFT` when it
  crafts, `AIPL` when it picks up a quest item.

### 3. Player context on the host

- Script stacks the host runs for a forwarded event (1) or story event (2), and for any engine event whose
  actor is a client's copy (the copy kills, hits, enters a trigger on the host), are tagged with that
  player. The tag follows what the stack starts: registrations made in it (`RegisterForSingleUpdate`, so
  the later `OnUpdate`), latent waits.
- In a tagged stack the VM sees the copy wherever it would see the player: `Game.GetPlayer()`, `PlayerREF`
  properties, an alias filled with the player, a player stored in a variable (the player's handle maps to
  the copy's, scoped to the stack), and comparisons (105220, scoped instead of global).
- Questions then read the copy, which must mirror its player's state: inventory, values, perks, spells,
  factions, level, location. That is a new requirement on the copy contract (equipment and values exist
  today).
- Effects on the copy are its owner's to apply (copy contract): a reward `AddItem` reaches that player's
  inventory, a `MoveTo` moves that player.
- Natives that act on the player implicitly (`Game.*` such as `AdvanceSkill`, `DisablePlayerControls`,
  `FadeOutGame`; `Debug.MessageBox`, `Debug.Notification`; UI calls) go to that player's game. The set is
  finite: every native passes the VM's registration (104788), so it can be listed and classified once.
- Native conditions evaluated for those events (story-node conditions such as "killer is the player",
  `GetIsReference PlayerRef`, run-on Player) map the copy to the player the same way, inside that
  evaluation. The condition functions that reference the player are a finite set: to be listed.

### 4. Engine code on the host that asks for the singleton

- What the host's engine processes natively with a copy (a kill, a hit, a crime witnessed by host NPCs, a
  trigger the copy enters) runs engine code that tests `g_thePlayer` directly: some of the 1,286 functions.
- Personal parts of those (XP, skill use, stats, kill credit) the client already applies on its own game
  (kill credit work: `client-*-kill-credit`).
- The world parts left: measure them, don't guess. During each client-action scenario, probe on the host
  which player-referencing functions run with a copy involved (the harness's probes). That turns "all of
  the engine" into a checked list, closed one function at a time.

### 5. Physical consequences: the client's change funnel

- What the client's engine changes directly in the world (an item taken from a shelf, a chest emptied, a
  lock picked, a book flagged read, a door's state) goes through its change funnel: the step 2 hook,
  14607/14608, the save system's record of every world change.
- The client sends those forms to the host as proposals. The host applies them (`LoadGame`) and every game
  gets them from the host.
- This also covers SKSE plugins that change world state in C++ on the client: whatever they change, the
  save system records.
- Decisions (quests, story events) never travel this way; they go through 1 and 2.

### 6. Actions inside the client's menus

- Crafting, reading, barter, lockpicking and dialogue run in the client's own menus. Their reactions reach
  the host through the layers above:
  - crafting raises `CRFT` (2) and PapyrusExtender's `OnItemCrafted` is a VM event (1);
  - the crafted item and the forge's state are change forms (5);
  - dialogue is the planned step sync.
- A plugin hook inside a menu is covered as long as it ends in a script event, a story event or a world
  change. What a plugin does in C++ only for the moment (UI, camera, sound) stays on the client, which is
  where it belongs.

## What it covers

| Consumer | Covered by | Left |
|---|---|---|
| Vanilla and mod scripts, any way they name the player | 1 and 3 | stacks started without a cause the tag can follow (to measure) |
| SKSE plugins' script events, mod events | 1 (same `SendEvent`) | none known |
| Story manager, all 30 event types | 2 and 3 | condition functions that test the player (to list) |
| Persistent world changes by anything on the client (engine, scripts, plugins) | 5 | forms created on the client (step 3's id remap); plugins' own co-save data |
| Engine code on the host that tests the singleton, for actions processed with a copy | 4 | the measured list |
| SKSE plugins' C++ reactions to host-side events with a copy | 4, per plugin | plugins that test the singleton in C++ for world effects (rare; found by the same probes) |

## Spikes, in order

1. **Count the traffic (no behaviour change):** hook `SendEvent` and `AddEvent_Impl` on the client and log,
   during the existing scenarios, every event that layers 1 and 2 would forward: how many, which, to which
   objects. Also confirm in IDA where the VM delivers the engine's own events.
2. **One lever:** the client activates an unpatched vanilla lever whose script gates on
   `== Game.GetPlayer()`; the event is forwarded, the host runs it in the client's context, and the gate
   passes. Then one trigger volume, one `PlayerREF` mod-style script.
3. **Probe the engine residue:** record on the host which player-referencing functions run during
   `melee-client-hits-host-npc`, `client-*-kill-credit` and a trigger scenario.
4. **One story event:** the client enters a location; its `CLOC` is raised on the host; the host's story
   manager starts the encounter, and the client gets it.
5. **Context through time:** a forwarded handler that registers for an update; its `OnUpdate` runs in the
   same player's context.

## Spike results (2026-10-19)

Harness-only code: `Code/client/TestHarness/PlayerEvents.cpp`. Its script-engine spike (commands
`player_events`, `player_context`, as committed in `6f1a5ff9`) became `Code/client/Games/Skyrim/Papyrus/GuestScripts.cpp`;
what stays is `player_probe`, `player_probe_read`, `story_trace`, `story_raise_location_change`.

### Spike 1: traffic (done)

- **Where to read events.** `SendEvent` (vfunc 0x24) sees every script event, but most have no script
  that handles them: 47,000 sent during `party-fast-travels`, about 580 delivered. The VM's per-object
  dispatch (vfunc 0x27, `DispatchMethodCall1`) builds the arguments only when the script has a non-empty
  handler of that name. They then go into `BSScript::Internal::RawFuncCallQuery`'s constructor (104943):
  the hook point with the arguments already built.
- **Do not build arguments in a hook.** Calling the arguments' functor from `SendEvent` (for every event)
  added lock orders the engine never takes there: `ExtraAliasInstanceArray` (`+0x182315`, held while the
  engine relays an event to a reference's aliases) then `ExtraDataList::GetExtraData` (`+0x17928d`), on both
  games. Gone once the arguments are read in 104943. With the spike's hooks off, the same scenarios show
  none of them; `+0xe85437 > +0x17928d` appears either way (vanilla).
- **What a client would forward.** Per scenario, 0 to 32 events, nearly all to **quest scripts on the
  player alias**: `OnLocationChange` (32 in fast travel), `OnObjectEquipped` / `OnObjectUnequipped`,
  `OnItemAdded`, `OnSpellCast`, `OnInit`. The player alias is the main channel, not levers.
- **What it would drop.** Events to world scripts not caused by the player: the client runs world scripts
  today (`OnUpdate` on quests, `OnPackageStart` / `OnPackageChange` on NPC copies and aliases): 80 to 540 per
  window. The host runs its own.
- **Story events on the client:** `SCPT`, `CLOC` (fast travel), `CAST`, `ASSU`, `KILL`. A kill raises
  `KILL` / `ASSU` on both games: on the host with the copy as killer, on the client for its own copy's
  death.
- **Ids.** The client's events name its own ids: its copy of a host NPC is a created form (`FF00081F`),
  so forwarding maps ids through the existing server ids.

### Spike 2: a lever, a trigger, a stored player (done)

The player context (harness-only): a script stack is **tagged** with a remote player's copy, and in a
tagged stack the VM sees the copy where it would see the player.

- **Tagging.** A stack is tagged when its bottom handler (`On...`) has a remote player's copy among its
  arguments, or when what queued its call was tagged (below).
- **`Game.GetPlayer()`** returns the copy in a tagged stack (hook on
  `NativeFunction0<StaticFunctionTag, Actor*>::MarshallAndDispatch`, 55618).
- **Comparisons** (`CodeTasklet`'s compare, 105220, whose `this` is the running tasklet and so gives the
  stack): in a tagged stack the player compares equal to that stack's copy. This covers a player stored
  in a variable or a `PlayerREF` property, which never calls `GetPlayer`. Scoped to the stack, not
  global as upstream's was.
- **Cause.** `NativeFunctionBase::Call` (104651) runs every native. While a tagged stack's native runs,
  the calls it queues (`RawFuncCallQuery`, 104943) are recorded for that copy; the stack that later runs
  one inherits the tag. `RegisterForSingleUpdate` records the target's next `OnUpdate` the same way.

| Test (vanilla, unpatched) | Context off, on the host | Context on, on the host |
|---|---|---|
| Rebel's Cairn sword `0xA05C0` (`dunRebelsCairnActivator`: `obj == Game.GetPlayer()`, then `GetLinkedRef().Activate(Self)`) | the gate rejects the copy; the linker `0xA05C4` gets nothing | the gate passes; the linker gets `OnActivate` twice, as on the client (4 runs) |
| Its linker's `ActivateBox` (`Game.GetPlayer().GetItemCount(sword)`, `RemoveItem`, `NoSwordMessage.Show()`), in a new stack started by `Activate(Self)` | asks the host's player | the stack inherits the copy; both `GetPlayer` calls return it |
| Soljund's Sinkhole boss trigger `0x6AEBE` (`obj == game.getPlayer()`, then `RegisterForSingleUpdate`; each `OnUpdate` casts the statue's fire at `game.getPlayer()` and registers again) | `OnTriggerEnter` only: no update | 6 chained `OnUpdate`s, each tagged; the fire targets the copy |
| Kolbjorn Barrow pressure tiles `0x0401DC0F` to `0x0401DC26` (`playerRef = Game.GetPlayer()` stored at `OnInit`, `akActivator == playerRef`) | never equal | "equal (scoped)"; `TurnPlateOn` runs (two plates). A plate the party leaves without stepping off stays in `InTrigger`, so each run needs a fresh one |

The copy reaches the host's triggers by movement sync alone: no forwarding needed for them. An earlier
run where the linker got nothing did not reproduce.

Found along the way:

- **Polling scripts have no cause.** `dunLabyrinthianMazeTabletScript`: `OnLoad` registers, and each
  `OnUpdate` asks `game.getPlayer().getDistance(self) < 512`. Nothing a player did starts it, so on the
  host it only ever sees the host's player. A context cannot help; this needs "any player" semantics.
- **Not tested: calls on the player.** `PlayerREF.GetItemCount(...)` with a stored player still acts on
  the host's player; swapping the call's object for the copy in `Call` (its top frame's `Self`) is the
  same mechanism, untested. `defaultDisplayItemInListScript` (display cases) is the example; it is not
  placed in the five masters.
- **Implicit-player natives** (`Message.Show`, `Debug.Notification`) show on the host: confirmed in the
  linker's `ActivateBox`.

### Spike 3: engine residue (done)

Count-only stubs (`lock inc`, plus a second counter when `rcx`, `rdx`, `r8` or `r9` is the copy) on the
1,285 functions that read `g_thePlayer` (Address Library IDs from IDA's xrefs to 403521; 24 skipped, hooked
by us already). MinHook places at most about 540 per session near the game, so three batches with a
reload between.

- **55 functions run on the host with the copy as an argument** during `melee-client-hits-host-npc`,
  `client-melee-kill-credit`, `client-spell-kill-credit` and the Soljund trigger, beyond what runs idle.
  Combat and death: `Actor::KillImpl`, `ActorProcess::unk_Assault_39359` (assault/crime),
  `fUnk_Attacked` (38626: faction reaction to the player, registers the attacker with the player),
  `Actor::CanAttackActor`, `Actor::ApplyAttackSpells`, `HitData` 44014, detection 40079,
  `Actor::StopCombat`. Movement: `MoveTo_Impl`, `MoveRefToNewSpace`, `GetCurrentLocation`. AI:
  `EndDialogue`, `EndInterruptPackage`, `ClearFurniture`. The rest are `Character::` updates without
  names yet.
- Each branches on "is this actor the player": kill credit and perks (`ApplyPerkEntries(9, g_thePlayer)`)
  in `KillImpl`, crime and faction reaction in the assault path, detection in 40079. That is the
  checked list for layer 4; classifying each (personal, already applied on the client; or world, the
  host must treat the copy as the player) is next.
- Two of the twelve scenario runs failed their post-check (an NPC's animation event and idle differed
  between the games right after); the probes only count, so this is likely the known animation drift
  and not caused by them. Not followed up.

### Spike 4: a forwarded story event (done)

The location change event (`CLOC`) is plain data: actor handle, old location, new location (built by
32288; its registered type index lives at 380069). On the client, `AddEvent_Impl` captures the player's
`CLOC`; on the host, the harness builds the same event for the copy and calls `AddEvent_Impl`. The story
manager's own log (32374, normally off) gives each node's and quest's outcome.

The party travelled QASmoke to the Bannered Mare. The host's own event and the forwarded one went
through the same 82 nodes with the same verdicts, with one difference: `WIChangeLocationNode` passed for
the forwarded event (why, not checked), then all its quests (`WIChangeLocation01` to `09`) failed their
conditions. Shared
quests already running (`WITavern`, `DialogueWhiterunArgueScene`) are not started again; quests that can
run twice (`BardSongStarter`, `CRHoldExpansion`, `WIGreeting`, `DLC1ScrollHandlingChangeLoc`) start again.

- **Forwarding works as is**: the event struct and `AddEvent_Impl` are all it takes.
- **The story manager tests conditions against the host's player.** The node visitor (32429) and the
  quest visitor (`BGSStoryManagerQuestFinder::VisitQuest`, 32430) build their condition parameters as
  `(g_thePlayer, g_thePlayer)`, whatever the event's actor. So a forwarded event is judged by the host's
  player's state (location, level, items). Layer 3's condition mapping is these two call sites while a
  forwarded event is processed; the event's own data (the actor alias) already carries the copy.
- Two events for one party move: if both games raise it, quests that can run twice run twice. Forward
  only what the host did not raise itself (a client arriving alone).

### Spike 5: context through time (done)

Soljund's trigger above: the tag passes from `OnTriggerEnter` through `RegisterForSingleUpdate` to each
`OnUpdate`, six in a row, every one re-registering from a tagged stack. Latent waits keep their stack, so
their tag. Queued calls (spike 2's linker) inherit the same way.

How a queued call is matched to the stack that runs it, in the spike: by (target object, function name),
first come first served. Two queued calls with the same target and name (the linker gets `OnActivate`
twice) leave the second untagged. The real link is where the VM turns a `RawFuncCallQuery` into a stack;
not found yet.

### What the spikes change in the design

- Layer 3 (player context) works for the three ways scripts name the player (`GetPlayer`, a stored
  variable or property, comparisons), through queued calls and updates. Still open: method calls on a
  stored player (above), and the query-to-stack link.
- Layer 2 (story events) works mechanically; it needs the two visitor call sites mapped.
- Layer 4 (engine residue) is now a list of 55 functions to classify, measured for melee, spells, kills
  and a trigger. Crime, dialogue and items are not measured yet.
- New gap: polling scripts that ask where the player is have no cause to follow (maze tablets, "is the
  player near" checks). They need the nearest or any player, not a context.

## Script scan: what follows a player check (2026-10-19)

`scripts/spikes/player_scan.py` over the 14,301 vanilla sources, a regex heuristic: handlers with their
same-script functions inlined two levels; "the player" is `GetPlayer()` or a variable or property holding
it. Placements counted over the five masters (refs and base forms). Player-alias quest scripts not
separated (spike 1's main channel).

| Handlers | Count | Identity only | Need a body |
|---|---|---|---|
| Checking the player after a player's action (activate, trigger, hit, read...) | 1,243 | 933 (75%) | 109 (9%) |
| Timers and loads using the player (no player action behind them) | 255 | 9 | 206, mostly position: `GetDistance` 82, `GetCurrentLocation` 30, `IsInLocation` 22 |
| Quest and scene fragments using the player | 771 | 11 | 176; 39 only teleport (`MoveTo` 59) |
| Dialogue fragments using the player | 481 | 8 | 15 |

The rest is items or gold (shared inventory), party effects (spells, factions, crime, hostility:
`NPC.StartCombat(player)` turns the NPC hostile and the host's AI picks the target) and messages (114 after
a player's action).

Trigger scripts checking the player: 432, placed 4,797 times.

- The common trap plate `TrapTriggerBase` (861) checks the player only when a reference sets
  `playerOnlyTrigger`, counts who is inside (`GetTriggerObjectCount`) and fires a physical trap.
- Aimed at the player: 9 scripts, 216 placements: `MQShoutTrigger` (Throat of the World fog, 101),
  `DLC01SoulCairnCratterScript` (42), `DLC2ApocryphaKillPlayerTrigScript` (41), `BWCLightDamageScript` (16),
  `MG07WaveTriggerScript` (13).
- Presence through a simple enter/leave switch, repeatable: 47 scripts; the most placed is
  `FXCameraAttachScript` (830), a camera effect while the player stands in a volume.
- Fire once: 192 scripts, 1,895 placements.

Decision from it: `docs/design/party-model.md`, "A guest's actions on the host: the simple version".

Drivers: `scripts/spikes/player_events.py` (`probe`, `story`), against a running duo with a test build; the
`activator`, `trigger` and `tile` drivers ran against the spike's `player_context` (`6f1a5ff9`).

## Sources

- [Skyrim Together Reborn playguide](https://wiki.tiltedphoques.com/skyrim-together-reborn/guides/playguide)
- [Skyrim Together Reborn](https://www.nexusmods.com/skyrimspecialedition/mods/69993)
- [Arcane University: Scripting Best Practices](https://wiki.beyondskyrim.org/wiki/Arcane_University:Scripting_Best_Practices) (`PlayerREF` over `GetPlayer`)
- [Fallout 4 multiplayer mods overview](https://dogwatergaming.com/fallout-multiplayer-mods/) (Fallout 4 Together: host-only progress)
- skymp `HeuristicPolicy.cpp`, PapyrusExtender `HookedEventHandler.cpp`, CommonLibSSE-NG
  `QuestEvents.h`, `ScriptEventSourceHolder.h`, `SKSE/RegistrationSet.h` (all under `reference/`)
