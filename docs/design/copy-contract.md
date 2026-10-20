# Copy contract

A **copy** is the stand-in a game shows for an actor another game owns. That covers a remote
player's character and the client's replica of a host NPC (`ActorExtension::IsRemote()`).

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
| Where it is (cell) | the owner's | spawned into the owner's cell (`CharacterSpawnRequest::CellId`), then moved by the sync |
| Faction membership (others' reactions to it) | yes | copies keep their factions; ally anger is our registry (Actor.cpp) |
| Being targeted, hit, damaged, killed | yes, by the owner | hits are requested from the owner (ActorValueService, MagicTarget hook) |
| Per-frame AI update (packages, procedures, combat and weapon decisions, crime, headtracking choice, movement controller) | no | `HookUpdateAI` (Actor.cpp) returns at the entry of `Character::UpdateAI` (37347) for copies and runs only its upkeep: voice line (37752, 37544), lip sync (39373/39377), essential/protected flags (37227), cell attachment (37352). Every callee is classified in actor-ai-and-copies.md. A copy holds the engine's do-nothing package (`HookCheckForNewPackage`, `Actor::InitiateDoNothingPackage` 37402), so an unseen one gets there too, as any NPC with a package does |
| Voice lines | the owner's | the owner's lines arrive as `NotifyDialogue` (the copy plays the file) and `NotifySubtitle` (the text over the copy); the copy's game picks none for it (it keeps `kCanSpeak` clear) |
| Movement integration | no | `HookApplyMovement` (Actor.cpp, `Actor::ApplyMovement` 37356): the sync positions copies |
| Body upkeep: `Actor::Update` (37348), animation graph job, movement job, magic casters and active effects, ragdoll | yes | engine, unchanged. `Actor::Update`'s decisions (sneak toggle, Surface package, water damage, swim and bleedout actions) are stopped by the action, package, damage and equip guards (audit in actor-ai-and-copies.md); `sneak-*-player` checks the sneak one |
| Animation graph updates while off-screen | every frame | `kForceAnimGraphUpdate` on copies (AnimationSystem), as Precision does for the actors it drives |
| Animation graph state at spawn | the owner's | the owner's graph state (as a savegame keeps it) goes with the spawn; the copy's fresh graph loads it instead of replaying the owner's last action (`AnimationGraphState`, actor-ai-and-copies.md) |
| Graph variables | inputs only, latest by owner tick | the variables the graph writes itself are not synced (`currentDefaultState`, `NotCasting`; see actor-ai-and-copies.md) |
| AI packages (picked or engine-created) | no | `HookCheckForNewPackage` and `HookPutCreatedPackage` (References.cpp) |
| Package sync from the owner | no | `AI_SYNC` stays 0 (References.cpp) |
| AI state others read (current package, sitting, sleeping, in a scene, alarmed, combat state) | the owner's, planned | decided 2026-10-19, not built: below, "AI state" |
| Applying spell effects locally | no | the owner applies them (MagicTarget.cpp) |
| Showing effects (burning, frost, glows) | visuals only | the owner lists its actors' active effects with visuals; the copy plays just their hit shader and art while listed (EffectVisualsService) |
| Hit feedback for the attacker (enemy health bar) | in the attacker's game | our player's hit on a copy shows its health bar at once, from the owner's health (CombatService::OnHitEvent) |
| Combat decisions, dialogue, reservations | no (audit when touched) | to check when syncing combat or dialogue |
| Crime | the party's, judged on the host | decided 2026-10-12: one shared party bounty. Any player's crime counts for everyone; the host's NPCs witness it. Not built: today only the host's own player can commit a crime on the host (a remote player's copy is an NPC to the engine, `Actor::UpdateTrespassing` runs for the real player only) |
| Followers, quests | undecided | decide when the system is synced |

## A remote player's copy: the deliberate exceptions

A copy is an NPC to the engine and to every mod: nothing makes it pass "is this the player"
(`g_thePlayer`, form id 0x14). A remote player's copy differs from an NPC copy in a few chosen places,
inherited from upstream Tilted Online and listed here so they stay choices:

| What | Why | Where |
|---|---|---|
| Essential, no bleedout recovery | it goes down like a player instead of dying; its owner decides | `Actor::SetPlayerRespawnMode`, `SkyrimBridge::SetActorPlayerRespawnMode` |
| Member of PlayerFaction (0xDB1, rank 1) | NPCs friendly to the player treat it as a friend; faction conditions in quests and mods count it as "on the player's side" (not as the player) | `Actor::SetPlayerRespawnMode` |
| A healer's Restoration perks carried to it | the target's owner applies the heal, so the caster's Regeneration (0x581F8) and Respite (0x581F9) go along with the request | `HookAddTarget`, `HookAdjustForPerks`, `HookHasPerk` (MagicTarget.cpp) |
| Fleeing always false; it speaks no AI dialogue responses | AI patches from before copies ran no AI: re-check whether they still do anything (actor-ai-and-copies.md, "Still open") | `HookIsFleeing`, `HookProcessResponse` (Actor.cpp) |
| Scripts that must accept any player | vanilla scripts that test `== Game.GetPlayer()` exclude the other player (puzzle doors, temple blessings, trainers); `SkyrimTogether.esp` overrides them with `SkyrimTogetherUtils.IsPlayer` / `IsRemotePlayer` (natives in BSScript.cpp) | `GameFiles/Skyrim/scripts/source` |

Rejected by upstream and here: making every Papyrus comparison `remotePlayer == Game.GetPlayer()`
true (`HookCompareVariables`, BSScript.cpp, not installed): "it messes up other things like
beastform". Scripts are patched one by one instead.

## Players' copies travel with the party (decided 2026-10-14)

Under the party tether everyone is together, so a copy of another player lives as long as that player is
in the session and travels with it, never with a cell. NPC copies (the host's actors) stay with their cell.

- **Persistent:** spawned with `TESForm::kPersistent` (`0x400`, what `TESForm::IsPersistent` 19593 tests;
  `SkyrimBridge::SetActorPersistent`), so the engine does not delete it when its cell unloads. Upstream
  copies are created references the engine deleted with their cell, and gave their form id to the next
  reference it made: travel used to drop every player copy and spawn it again, and a stale form id once
  drove a Cow (`docs/re/crashes/2026-10-14-reused-copy-form-id.md`).
- **Not saved:** every copy keeps `Actor::Create`'s skip-save in-game flags (`0xFFFF`, upstream; which bit
  does it is not traced). Checked 2026-10-14: the host saved with a persistent copy in its cell, and the
  save booted alone has no copy. `BGSSaveLoadGame::AddChangeFlags` (the funnel of every recorded change)
  also skips form flag `0x4000`; copies do not have it.
- **Moved, not respawned:** an arrival drops NPC copies left behind but keeps the players' copies;
  `RunPresence` moves each to where its owner says it is, at once after an arrival. The host sends an
  arriving player the host's actors there, never players' characters (`HostCharacters::OnEnterCell`); a
  player that arrived ahead of the host gets them when the host arrives.
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
  (`ActorExtension::IsRemote`, set at spawn; a new actor has a fresh extension). Otherwise it is gone: its
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

## AI state: the owner's, for anything that asks (decided 2026-10-19, not built)

A copy runs no AI, but what asks about the actor must get the owner's answer: a dialogue line's conditions on
its speaker, a greeting, a scene's. **No game but the owner's simulates the actor; every game knows its AI state.**
The owner sends the few facts conditions read when they change (current package, sitting or sleeping, in a
scene, alarmed, combat state), and on a copy those condition functions answer from them: one hook each in the
condition function table, "a copy: the owner's state". Nothing runs the package on the copy.

First user: shared dialogue syncing the step (`shared-dialogue.md`, "Next"), where each game judges its menu's
topics against the NPC's copy. Measured on Skyrim and its DLC: about 200 of 15,449 menu lines test it,
`GetIsCurrentPackage` the most (`scripts/re/dialogue_conditions.py`, which also counts a mod list's).

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
