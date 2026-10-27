# Copy contract

A **copy** is the stand-in a game shows for an actor another game owns. That covers another player's
character (an other player) and a guest's copy of a host NPC (`ActorExtension::IsCopy()`). Words:
`glossary.md`.

Skyrim has exactly one player: `g_thePlayer` is read in 2843 places across 1286 engine functions.
So remote players can only be NPC-bodied actors. The engine then treats copies like any NPC and
runs code paths vanilla never runs with a "player-like" actor. Each system we sync adds more of
those meetings.

**The rule:** a copy is a puppet with an NPC body. The owner's game decides everything it does.
The copy shows the result and takes part in what *other* actors do to it, nothing more.

## What a copy keeps and what it gives up

| System | Copy takes part? | How it is enforced / where |
|---|---|---|
| Body, 3D, animation graph, collision | yes | the movement and animation sync drive it |
| Where it is (cell) | the owner's | spawned into the owner's cell (`NotifyCharacterSpawn::CellId`), then moved by the sync |
| Faction membership (others' reactions to it) | yes | copies keep their factions; ally anger is our registry (Actor.cpp) |
| Being targeted, hit, damaged, killed | yes, by the owner | hits are requested from the owner (ActorValueService, MagicTarget hook) |
| Actor values, skills included | the owner's | `ActorValueService` sends every value the owner's actor changes (health, magicka and stamina always); on the host a guest's script reading `Game.GetPlayer().GetAv("Speechcraft")` reads that guest's Speech (`dialogue-picker-gets-speech-experience`, 2026-10-22) |
| Its base as an owner | as the player's | a copy's base is a created form, which the engine refuses as an owner (`ExtraDataList::SetOwnerForm`, 11609): on the host such an owner is written as the player's base (0x7), each game's own player, so what a script gives a guest to own (a bed, a horse) is the party's (`party-model.md`, "Items and gold") |
| Per-frame AI update (packages, procedures, combat and weapon decisions, crime, headtracking choice, movement controller) | no | `HookUpdateAI` (Actor.cpp) returns at the entry of `Character::UpdateAI` (37347) for copies and runs only its upkeep: voice line (37752, 37544), lip sync (39373/39377), essential/protected flags (37227), cell attachment (37352). Every callee is classified in actor-ai-and-copies.md. A copy holds the engine's do-nothing package (`HookCheckForNewPackage`, `Actor::InitiateDoNothingPackage` 37402), so an unseen one gets there too, as any NPC with a package does |
| Voice lines | the owner's | the owner's lines arrive as `NotifyDialogue` (the copy plays the file) and `NotifySubtitle` (the text over the copy); the copy's game picks none for it (it keeps `kCanSpeak` clear) |
| Movement integration | no | `HookApplyMovement` (Actor.cpp, `Actor::ApplyMovement` 37356): the sync positions copies |
| Body upkeep: `Actor::Update` (37348), animation graph job, movement job, magic casters and active effects, ragdoll | yes | engine, unchanged. `Actor::Update`'s decisions (sneak toggle, Surface package, water damage, swim and bleedout actions) are stopped by the action, package, damage and equip guards (audit in actor-ai-and-copies.md); `sneak-*-player` checks the sneak one |
| Animation graph updates while off-screen | every frame | `kForceAnimGraphUpdate` on copies (AnimationSystem), as Precision does for the actors it drives |
| Animation graph state at spawn | the owner's | the owner's graph state (as a savegame keeps it) goes with the spawn; the copy's fresh graph loads it instead of replaying the owner's last action (`AnimationGraphState`, actor-ai-and-copies.md) |
| Graph variables | inputs only, latest by owner tick | the variables the graph writes itself are not synced (`currentDefaultState`, `NotCasting`; see actor-ai-and-copies.md) |
| AI packages (picked or engine-created) | no | `HookCheckForNewPackage` and `HookPutCreatedPackage` (References.cpp) |
| The owner's package | held, never run | the owner's form package in the copy's running slot, no target (`Actor::PutOwnerPackage`, `ActorPackage::InitFromPackage` 38959), so conditions, Papyrus and plugins reading the slot inline see it; `UpdateAI`, which runs packages, is cut; none or an engine-made one: the do-nothing package. Upstream's package relay (`AI_SYNC`, `NewPackageRequest`) is deleted |
| AI state others read (current package, sitting, sleeping, in a scene, alarmed, combat state) | the owner's | built 2026-10-22 (`AiStateService`, `Games/Skyrim/CopyAiState.cpp`): package, scene, combat state and target; sitting and sleeping by replay; alarmed not yet. Below, "AI state" |
| Combat controller, combat group membership | no | never given to a copy: `CombatUpdateJob` (46902) would run its combat AI (below, "AI state") |
| Applying spell effects locally | no | the owner applies them (MagicTarget.cpp); on the host, the world's spells on a guest's character (a statue, a trap, a script making the copy cast) are sent to its owner too (`docs/re/systems/guest-world-events.md`) |
| Showing effects (burning, frost, glows) | visuals only | the owner lists its actors' active effects whose visuals last (FX Persist) when its engine starts or stops them, a spawn carrying the list; the copy plays just their hit shader and art while listed, again on its 3D loading; visuals that play once go out as their effect starts, and the copy plays them once (EffectVisualsService). Visuals the engine plays at a game's camera on its player's behalf (a VisualEffect attached to the camera, `FXCameraAttachScript`'s fluff 0xB94EC; the effect's controller answers `EffectAttachesToCamera`, vtable 0x1A) are that screen's: no copy shows them, and the harness reports them apart (`mod.camera_visuals`) |
| Hit feedback for the attacker (enemy health bar) | in the attacker's game | our player's hit on a copy shows its health bar at once, from the owner's health (CombatService::OnHitEvent) |
| Combat decisions, dialogue, reservations | no (audit when touched) | to check when syncing combat or dialogue |
| Crime | the party's, judged on the host | `CrimeService`, `HostCrime` and `CrimeBridge` (2026-10-26, shared-party-bounty.md): guest theft/pickpocket/lock proposals are judged by host witnesses; host-native alarms adapt a guest culprit's detection without changing the native crime's host-player handle. The service refreshes guest-copy trespass state separately from decision-making AI. Absolute per-faction bounty mirrors to guests; guest fines debit the party's purse once (`party-purse.md`) with owner-side stolen-item confiscation. Native coverage and remaining jail/trespass-AI limits are recorded in the design doc |
| Player mounts | rider-owned while ridden, host-owned after dismount; copies retained through party travel | `CharacterService`, `HostCharacters`; a native MoveTo can dismount implicitly, and the handoff includes the horse's last owned placement (`party-travel.md`) |
| NPC followers | design approved; implementation deferred | `followers.md` |
| Quests | undecided | decide when the system is synced |

## A remote player's copy: the deliberate exceptions

A copy is an NPC to the engine and to every mod: nothing makes it pass "is this the player"
(`g_thePlayer`, form id 0x14). A remote player's copy differs from an NPC copy in a few chosen places,
inherited from upstream Tilted Online and listed here so they stay choices:

| What | Why | Where |
|---|---|---|
| Member of PlayerFaction (0xDB1, rank 1) | the player's enemies fight it and its friends treat it as a friend (`ReactToAttack` tolerates a friend's hits by that reaction); faction conditions in quests and mods count it as "on the player's side" (not as the player). Not essential since 2026-10-26: it dies by its owner's death action (`docs/design/death.md`) | `Actor::JoinPlayerFaction`, `SkyrimBridge::SetActorPlayerFaction` |
| A healer's Restoration perks carried to it | the target's owner applies the heal, so the caster's Regeneration (0x581F8) and Respite (0x581F9) go along with the request | `HookAddTarget`, `HookAdjustForPerks`, `HookHasPerk` (MagicTarget.cpp) |
| Fleeing always false; it speaks no AI dialogue responses | AI patches from before copies ran no AI: re-check whether they still do anything (actor-ai-and-copies.md, "Still open") | `HookIsFleeing`, `HookProcessResponse` (Actor.cpp) |

On the host every guest player is the player in scripts, and `Game.GetPlayer()` is the player
whose action started the script (2026-10-19, `docs/design/party-model.md`, "A guest's actions on the
host"; `Games/Skyrim/Papyrus/GuestScripts.cpp`). Upstream made only the comparison true, globally, and dropped it:
"it messes up other things like beastform" (`GetPlayer()` still named the host, so the effect after the
check landed on the wrong body). Upstream's patched vanilla scripts (claw doors, temple shrines, Vilkas's trial)
and their `SkyrimTogetherUtils` natives were removed for it (2026-10-20); each has a scenario
(`docs/re/systems/papyrus-guest-scripts.md`, "Tests").

## Changes on an existing copy

An owner's level notification writes its player's copy's NPC base level, with change flag `0x2`; a later spawn
uses the latest roster level. A look change updates the same actor and temporary NPC base through FormState,
weight, runtime race/3D and FaceGen tints. Rebuilding happens between frames and invalidates the old graph's
variable descriptor. Dialogue and follower references keep the same actor. Player recovery remains a replacement.
Instant fire-and-forget FX Persist effects use a flash when the active-effect list cannot retain them.

Tests and engine details: [small-bugs.md](small-bugs.md).

## Players' copies travel with the party (decided 2026-10-14)

Under the party tether everyone is together, so a copy of another player lives as long as that player is
in the session and travels with it, never with a cell. NPC copies (the host's actors) stay with their cell.

- **Persistent:** spawned with `TESForm::kPersistent` (`0x400`, what `TESForm::IsPersistent` 19593 tests;
  `SkyrimBridge::SetActorPersistent`), so the engine does not delete it when its cell unloads. Upstream
  copies are created references the engine deleted with their cell, and gave their form id to the next
  reference it made: travel used to drop every other player's copy and spawn it again, and a stale form id once
  drove a Cow (`docs/re/crashes/2026-10-14-reused-copy-form-id.md`).
- **Not saved:** every copy keeps `Actor::Create`'s skip-save in-game flags (`0xFFFF`, upstream; which bit
  does it is not traced). Checked 2026-10-14: the host saved with a persistent copy in its cell, and the
  save booted alone has no copy. `BGSSaveLoadGame::AddChangeFlags` (the funnel of every recorded change)
  also skips form flag `0x4000`; copies do not have it.
- **Moved, not respawned:** an arrival drops NPC copies left behind but keeps the players' copies;
  `RunPresence` moves each to where its owner says it is, at once after an arrival. The host sends an
  arriving player the host's actors there, never players' characters (`HostCharacters::OnEnterCell`); a
  player that arrived ahead of the host gets them when the host arrives.
- **Arrival races (found 2026-10-23 by tier 2, `travel_flows.cpp`).** An NPC the host's game announced in the frame
  a following player's arrival reached the host's rules was in neither the announcement (that player was not with
  the host yet) nor the arrival's spawns (the host's game handles its id on its next update): that player never
  got its copy. Now the arrival leaves the player waiting on any host actor its game has not moved yet, and its
  first movement sends it the spawn. And the host's own arrival sent a player ahead of it the NPCs of the cell the
  host had just left (discovery drops them on its next update): a held reference was flagged a copy again, a
  placeatme NPC's copy made in the guest's cell. `BuildSpawnRequests` skips an NPC without 3D.
- **Joining elsewhere (2026-10-23).** A guest whose player announces itself away from the host's, or outdoors
  out of its loaded grid (`InHostGrid`), is sent to it (`NotifyPartyTravel`, `HostCharacters::BringToParty`); its
  arrival there sends the players' characters both ways (`SendPlayersMet`: the copies made at its join may be
  gone); the copies it got on joining that landed in its own cell
  go with its arrival, and the host sends it the actors there.
- **No inventory reset:** enabling an actor (`TESObjectREFR::Enable` 19800, which a 3D reload goes through)
  resets its inventory to its base's template (`Actor::ResetInventory` 37322 → `InitInventoryIfRequired`
  16038 → `Actor::RemoveOutfitItems` 19690): the outfit is removed and put on again, which the equip hook
  refuses for a copy. A player's copy has the player's base outfit, so this stripped the owner's own
  clothes for up to 3 s (until the owner's next equipment snapshot), and the removals went to the owner
  as inventory changes. A copy's inventory is its owner's: `HookResetInventory` (Actor.cpp) skips the reset
  for a copy. Checked by disabling and enabling a copy (its 3D reloaded): its worn set stays its owner's.
- **Removed** when its player leaves or the session ends (`NotifyRemoveCharacter`, a disconnect drops every
  copy); never by discovery noticing it left the loaded area.
- **Identity:** a copy is the actor at its form id only while that actor is flagged a copy
  (`ActorExtension::IsCopy`, set at spawn; a new actor has a fresh extension). Otherwise it is gone: its
  owner around, it is spawned again; elsewhere, dropped. Nothing moves, voices or deletes a stranger.

## How a copy runs no AI (decided 2026-10-11)

Full map: `docs/re/systems/actor-ai-and-copies.md`. In short:

- The engine's own "AI off" is `Actor::EnableAI(false)` (37504), which clears `boolBits` bit
  `kProcessMe`. Every reader of that bit was found in the exe (a scan of all code, checked in IDA):
  besides the console and Papyrus toggles, they are the per-frame passes of `ProcessLists`. With the
  bit clear, an actor loses its AI update (`Character::UpdateAI` 37347) **and** `Actor::Update`
  (replaced by `UpdateNoAI`) **and** its animation graph job (`RunOneActorAnimationUpdateJob` 41453 is
  queued only for actors that have it). That is the documented "EnableAI freezes the model". A copy
  needs its graph, so `kProcessMe` stays set.
- `Character::UpdateAI`'s first test is `kProcessMe`. Returning at its entry for copies is exactly
  what `EnableAI(false)` does to the AI, without touching the body jobs. Nearly everything in it is a
  decision: package evaluation, `AIProcess::UpdatePackageAndProcess` (39108) with the high-process
  AI that sheathed copies every frame (39637), crime, favours, headtracking choice, the movement
  controller's update, essential-down recovery, respawn. The audit of all its callees (2026-10-12,
  actor-ai-and-copies.md) found four pieces of upkeep, which `HookUpdateAI` runs for copies under
  UpdateAI's own conditions: the voice line, lip sync for races without a FaceGen head, the
  essential/protected flags, the cell attachment. Its body-like part for non-high actors (casters,
  active effects, regen: 38785) runs for high actors from `Job_Combat_and_magic` anyway.
- Nothing may rely on the AI to correct a copy. It did once: copies spawned into the player's cell
  were walked back to their package location. A copy of a game-data reference now spawns in its
  owner's cell (`OnCharacterSpawn`).
- `SetPlayerControls` is `EnableAI(false)` plus a controls-driven movement controller: the same
  freeze, and local input would drive the copy. Not usable.
- Prior art does not cut the AI: SkyMP keeps it on and overrides `DrawWeaponMagicHands`' argument
  (a symptom filter like the reverted one); upstream Tilted Online skipped only `ApplyMovement`.

## AI state: the owner's, for anything that asks (decided 2026-10-19, revised 2026-10-20, built 2026-10-22)

A copy runs no AI, but whatever asks about the actor must get the owner's answer, the same as on the owner's
game: a dialogue line's conditions on its speaker, a quest script, the engine itself, and later mods (Open
Animation Replacer picks clips from these on every game). **No game but the owner's simulates the actor; every
game reads its AI state 1:1.**

```
owner, when it changes:   send(actor, {package, package target, combat: none/fighting/searching,
                                        combat target, sit/sleep state and furniture, scene,
                                        talking/greeting, follow target})

copy, on receiving it:    a field readers read directly   -> write the owner's value into it
                          an answer an engine object gives -> hook the getter, answer from the record
```

Revised 2026-10-20: the first plan hooked the condition functions. That answers vanilla conditions only;
Papyrus natives, the engine and CommonLibSSE-NG plugins read the fields and getters directly and would still
get the copy's own answer.

- **Fields** (package, sit/sleep state, combat target, the searching bit, greeting, scene): read inline by
  their readers. Writing them is safe only while nothing a copy runs acts on them; `UpdateAI`, which acts on
  them, is cut. To check field by field before writing it.
- **Combat state is an object.** `Actor::IsInCombat` (38562) is "has a combat controller that has not
  stopped", `GetCombatState` (38556) adds the combat group's search state. Combat controllers are updated
  by `CombatUpdateJob` (46902) for every combat group member, outside `UpdateAI`: a copy given a real
  controller or group would run combat AI. So a copy never gets one; `IsInCombat` and `GetCombatState` are
  hooked for copies. Before the hook: 22 of the 100 engine functions calling `IsInCombat` also touch the
  controller field, and each must be checked not to use it after a "yes" (actor-ai-and-copies.md,
  "Combat state and its readers").
- **The host's AI aimed at a guest's player.** A guest's own player counts as in combat from the combat groups
  targeting it (41238, from 41261). On a guest those are copies with no groups, so the guest's player was never
  in combat. Built 2026-10-22, not as first planned (a count sent by the host): each combatant's AI state already
  says it fights that guest's character, so the guest counts its copies in combat with its player.
- **Forms go by id** (packages, furniture, scenes), so a mod's package travels as a vanilla one does.

**As built (2026-10-22).** `AiStateService` (every game), event-driven: the engine's package, combat, death and
scene events, a combat target switch (`CombatController::SetTarget` 33235) and the player's in-combat flag
(`PlayerCharacter::UpdateInCombat` 41238), all in `Games/Skyrim/AiStateEvents.cpp`, mark an owned actor changed; its state is read on the main thread and
sent (`AiStateRequest`) when it differs from the last sent. A spawn carries it. The host relays it from the owner only
(`NotifyAiState`). No timed check, no periodic resend. The copy's side, `IActorBridge::SetCopyAiFields` and
`Games/Skyrim/CopyAiState.cpp`:

```
owner's game                              copy's game
TESPackageEvent / TESCombatEvent /        NotifyAiState, or the spawn's AiState
TESDeathEvent / TESSceneEvent,              -> SetCopyAiFields:
SetTarget, the player's flag (any
  thread) -> AiStateChangeEvent (queued)       package  -> running slot
  -> read (main thread), send if changed      scene, combat -> ActorExtension, answered
                                              combat target -> combatHandle
                                            -> SetCopiesFightingPlayer(count)
```


```
package          the owner's form package in the running slot (InitFromPackage 38959), no target;
                 nothing runs it (UpdateAI is cut). Every reader sees it, inline ones too (OAR)
scene            Actor::GetCurrentScene (37247) hooked: the owner's
combat state     IsInCombat (38562) and GetCombatState (38556) hooked: the owner's to the condition
                 functions and plugins; the engine's own callers (which use the controller after a yes,
                 often through EngagingInCombat 38563) get the copy's own
combat target    Actor::currentCombatTarget (+0x104) written; the engine clears it again (open)
player in combat 41261 hooked: copies fighting this game's player count as combat-group members
furniture, follow target   not sent (no vanilla reader found); shown in the status's `ai`
sit/sleep, talking         the owner's already, by replay
alarmed          not carried (an engine-made package)
```

Readouts: the status's `ai_state` (package, scene, combat, combat target, in combat; `EngineCalls::GetAiReadout`, as
conditions read them), compared by the desync check; harness `is_current_package`, `put_package` (which sends the
package event, as evaluation does). Scenarios `copy-runs-owners-package`,
`copy-in-owners-combat`, `guest-player-in-hosts-combat`, `guest-player-in-switched-combat`. Details and the audit:
`docs/re/systems/actor-ai-and-copies.md`, "Combat state and its readers".

First user: shared dialogue syncing the step (`shared-dialogue.md`, "Next"), where each game judges its menu's
topics against the NPC's copy. Measured on Skyrim and its DLC: about 200 of 15,449 menu lines test it,
`GetIsCurrentPackage` the most (`scripts/re/dialogue_conditions.py`, which also counts a mod list's).

**Vanilla first, mods later (2026-10-20).** Built for vanilla now, without choices a mod would undo. Not built
until mods are taken on, measured on a modded setup first:
- **Graph events and graph inputs from outside the actions.** AI and combat mods decide where the AI or the hit
  runs (the host) and act through `NotifyAnimationGraph` and `SetGraphVariable*` (Valhalla Combat's stagger,
  TDM, Papyrus `SendAnimationEvent`); a copy replays only `PerformAction`. Every event sent to a graph goes
  through `IAnimationGraphManagerHolder::NotifyAnimationGraph` (32901), so one hook on the owner can send the
  ones no action sent, beside the action replay (`graph-replication.md`).
- **Open Animation Replacer's random picks** and time-based conditions; OAR's API reads the owner's pick
  (`GetCurrentReplacementAnimationInfo`), forcing it on a copy needs a hook into OAR.
- **Mods' private per-actor state** (poise, custom stamina): its effects arrive as actions, values and events;
  a query of it on a guest gets the guest's answer.

## When the owner's actor goes away (found 2026-10-20, decided 2026-10-22: the copy follows its owner)

A copy of one of the game's own NPCs outlived its owner's actor (fixed 2026-10-22, below).

- **What happens.** The host's NPC leaves the host's game (walks out of the loaded area, is moved away, is
  disabled): the server removes it, and each client's `CharacterService::OnRemoveCharacter` deletes the copy
  only if the client spawned it (a temporary reference: `IsActorTemporary`, `IsActorNpcTemporary`). A copy of a
  placed NPC (Vilkas, `0x1A695`) is the client's own reference from the game data: it only loses its copy
  components (`DeleteRemoteEntityComponents`) and stays where the copy last stood.
- **What the client then has.** Its game treats that NPC as its own actor: the status lists it under the
  client's own actors, the baseline check reports "client still owns NPCs ['0001A695']", and a later run saw
  "copy in client with no owner in the other game". In play: a ghost the host no longer has, standing (and
  possibly acting) on the client where the host's NPC was last, and a client simulating a host NPC, which
  the direct authority model rules out.
- **When it happens in play.** Any placed NPC leaving the party's cells on the host: through a load door, by a
  quest's `MoveTo` (Vilkas sent for the player), by `Disable`, by its schedule taking it out of the loaded
  area.
- **Repro.** `vilkas-counts-client-hits` without the client's half of `release_host_npc` (coop_test.py): stage
  Vilkas, then send only the host's Vilkas home (`move_actor` with `home`); the client's Vilkas stays on the
  stage as its own.
- **Decided (2026-10-22): the copy does what its owner does.** The removal carries where the host's NPC is
  now (its cell or worldspace and position, or that it was disabled or deleted); the guest moves its reference
  there (or disables it), still a copy with no AI of its own, and the host's next spawn of it takes over. A
  guest never runs a host NPC.
- **Built (2026-10-22).** The host's game reads where its actor is as it stops simulating it
  (`CharacterService::ReadWhereabouts`: cell, worldspace, position and heading, or disabled, or deleted) and sends
  it with the removal (`RequestRemoveCharacter`/`NotifyRemoveCharacter::Whereabouts`, `ActorWhereabouts`). The
  guest's `FollowOwner` moves its reference there (skipped within 64 units in the same cell), or disables it
  (deleted: disabled and marked deleted), and keeps it flagged a copy (`HeldCopyComponent`) until the host's next
  spawn of it or the session's end. A game reference dropped on an arrival is held the same way: before, it lost
  its copy flag and the guest ran that NPC's AI wherever it was left. The copy follows where the host's actor was
  at removal; an unloaded host NPC walking on (its schedule) is not followed until it is spawned again.
  Scenario `host-npc-leaves-copy-follows`; `release_host_npc` no longer touches the client.
- **Gone before its assignment came back (found 2026-10-23 by tier 2, `copy_flows.cpp`; gone with the answer the same
  day: the host's game owns an actor from its announcement).** The host's rules send a
  new NPC's spawn to the guests with its assignment, and the host's own game handles the answer on its next
  update: a guest has its copy a frame before the host's game knows it owns the NPC. An NPC unloading in that
  frame was dropped by `OnAssignCharacter` ("actor not found") with no removal sent, and the guest's copy stayed
  with no owner. Now `OnAssignCharacter` sends `RequestRemoveCharacter` for the session id it was given
  (whereabouts unknown: nothing to follow, the actor is gone).

## When syncing a new system

1. Decide which row it falls under, before writing sync code.
2. If copies must not take part, block it at the engine's single entry point for that system,
   in one hook, and add the row here.
3. Run the scenarios with the lock checker on (`coop_test.py locks`). A new pattern with our
   code on the stack fails the scenario.

## Why this exists

On 2026-10-10 the host deadlocked when it disabled an NPC that was fighting the client's copy.
1. The engine's Disable cleanup (41331) holds the player's read lock while it ends packages
   aimed at the disabled NPC.
2. The copy had a package the engine created for it ("movement blocked", flee) that reserves
   the player.
3. Resuming that package asked for the player's write lock on the same thread.
The lock checker named it on the first hang. The fix was this contract's package rows.
