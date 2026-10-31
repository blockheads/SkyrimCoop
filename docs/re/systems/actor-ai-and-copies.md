# Actor AI, the per-frame update, and copies

How the engine updates an NPC every frame, what SkyrimCoop cuts for **copies** (actors another game
owns, `ActorExtension::IsCopy()`), and how a replayed action reaches a copy's behaviour graph. IDs
are Address Library IDs for 1.6.1170; names marked "our name" have no community name (CommonLib,
meh321, the reference plugins and a web search had only `sub_`). Every name below is in IDA and in
`docs/re/ida_notes.tsv`. Decision and contract: `docs/design/copy-contract.md`.

## The per-frame update of a high-process actor

`ProcessLists` (CommonLib layout: high/low/middle-high/middle-low handle arrays at +0x30/+0x48/+0x60/
+0x78) feeds the frame's job lists. The job entries carry Bethesda's own job names:

| Job | Per high actor | Gated on `kProcessMe` |
|---|---|---|
| `Job_Actor_update` (39050) → `QueueHighActorUpdates` (41367) → `UpdateActor` (41448) → `ProcessLists::UpdateHighActor` (41369) | if processable: `Character::UpdateAI` (37347) (or, off-screen with no package, only `checkForNewPackage`), `NearbyActors::Insert`, follower timers, then `Actor::Update` (37348, vfunc 0xAD); otherwise `Actor::UpdateNoAI` (37349, vfunc 0xAE → `UpdateActor3DPosition`) | yes |
| `Job_Actor_movement` (39053) → `QueueHighActorMovement` (41306) → `UpdateActorMovementJob` (41444) → `Actor::UpdateMovement` (37350) | movement controller update, `Actor::ApplyMovement` (37356), character controller settings | no |
| `Job_Actor_animation` (39054) → `QueueHighActorAnimation` (41370) → `RunOneActorAnimationUpdateJob` (41453) | animation graph update (`TESObjectREFR::UpdateAnimation` 20119 via 37364) | yes (list built by 41466) |
| `Job_Combat_and_magic` (39065) → `UpdateHighActorsMagic` (41372) → `Character::UpdateMagicAndRegen` (38785) | magic casters, `MagicTarget::UpdateActiveEffects`, regen | no |
| `Job_Ragdoll_post_physics` (39068) | `UpdateRagdollPostPhysics` (41447) → `UpdateNoAI` | no |
| `Job_Non_render_safe_AI` (39089) | `ProcessActorAnimSG`, `Actor::UpdateNonRenderSafe` (37360) | no |

Middle-high, middle-low and low actors get `Character::UpdateAI` time-sliced from 41305, 41304 and
41302, which also need `kProcessMe`.

"Processable" is `!kInTempChangeList (boolBits 0x200) && !(form flags 0x200000) && kProcessMe
(boolBits 0x2) && currentProcess`. `boolBits` is CommonLib's `Actor::boolBits` (AE +0xE8, our
`Actor::flags1`).

### Every reader of `kProcessMe`

A scan of the whole code section (capstone, with register copies followed), checked one by one in IDA:
`Character::UpdateAI` (its second test), `ProcessLists::UpdateHighActor` (41369), the three lower-level
AI passes (41302/41304/41305), `CollectAnimatedHighActors` (41466, the animation job list),
`UpdateHighActorsAnimationSerial` (41377, vfunc 0x7D `UpdateAnimation`), `CheckHighActorsDetectPlayer`
(41409), `UpdateFollowPackages` (41295), `SendMovementMessage` (41656), 33216 (an AI condition), and
the console/Papyrus toggles (`ToggleAI`, `ToggleActorsAI`, `IsActorsAIOff`, `SetActorsAI`). The only
writers are the actor constructors (initial `boolBits` 0x10602182 includes it) and `Actor::EnableAI`
(37504, also called by the `EnableAI` and `SetPlayerControls` natives).

So `EnableAI(false)` stops the AI **and** `Actor::Update` **and** the animation graph job. That is the
CK wiki's "disabling AI disables havok processing" and the model left behind while the collision
moves: the graph and the 3D update stop, while `ApplyMovement` and the character controller keep
running.

### What `Character::UpdateAI` (37347) does

1. Returns at once if the form is disabled (flag 0x800) or `kProcessMe` is clear.
2. Scene and voice line upkeep, favour following, the headtracking choice (38011 →
   `kShouldRotateToTrack`), timers, essential/protected flags, lip sync.
3. For actors not in high process: the movement controller (+0x150, vfunc 8) and 38785.
4. Then, alive: package evaluation, crime and faction bookkeeping, the do-nothing package, and
   `AIProcess::UpdatePackageAndProcess` (39108). That runs the per-level AI first (high:
   `AIProcess::UpdateHighProcessAI` 39637 with the weapon draw/sheathe decision that told every copy
   to sheathe) and then the package procedures (heading, travel, `MoveRefToNewSpace`).
5. Dead or down: essential recovery (`SetLifeState(0)`), respawn queueing.
6. Last, for an actor below high process: `Character::UpdateCellAttachment` (37352, our name). In an
   exterior it loads or unloads the actor's 3D to match whether its cell is attached, or queues a
   move to the exterior cell of the same worldspace it stands in; in an interior it does nothing.
   That is world upkeep, not a decision, and its only other caller is `Actor::UpdateNonRenderSafe`
   (37360), which runs for high actors only.

The Dying→Dead change (`SetLifeState(2)`) is not in here: `KillImmediate` and two `ProcessLists`
functions do it. The player runs this function too, from `PlayerCharacter` 40748; its movement
controller is controls-driven and its package is the player's.

A high actor reaches it only when `Character::ShouldOnlyCheckPackage` (37277, our name) is false. That
is true for an actor with no package (or a Wander/Travel one, types 5/6), no quest alias and no pending
forced update (middle process +0x31D), whose 3D is far enough from the camera to fade
(`BSFadeNode::sub_105490` compares the distance to the camera with the node's fade radius; it does not
test the view: 1000 units away and behind the player still counts as seen in QASmoke). `ProcessLists::
UpdateHighActor` then runs only `checkForNewPackage` (39114). Every NPC has a package. The player has
none (`mod.package_type` -1): the main loop updates it, not `UpdateHighActor`.

### Every callee, decision or upkeep (audited 2026-10-12)

UpdateAI makes 82 calls to 65 functions, plus virtual calls. Each was looked up first in
CommonLibSSE-NG, meh321's names, the plugins in `reference/plugins` (SkyMP included) and a GitHub
code search of every ID (`RELOCATION_ID(se, ae)` with the ID in the AE slot; hits where it is an SE ID
of another function were dropped). Then IDA for the rest. Names marked "ours" had none anywhere; all
are in IDA and `docs/re/ida_notes.tsv`.

**Decision**: what the actor chooses to do; its owner's game does it. **Upkeep**: what its body or the
world needs whoever decides; a copy runs it from `HookUpdateAI`. **Helper**: no effect of its own.

| ID | Name (source) | What it does there | Class | Copy |
|---|---|---|---|---|
| vfunc 0x4A, 24126, vfunc 0x4B | `GetCurrentScene` (CL), `BGSScene::UpdatePhase` (ours; meh: `BGSScene::sub_24126`), `SetCurrentScene` (CL) | With `boolBits` 8: advances the actor's running scene, or drops it | decision (quest scenes, the host's) | no |
| vfunc 0x99 | `IsDead` (CL) | Sets `boolFlags` `kCanSpeak` (1<<13, CL) when alive, not knocked down, not unconscious. Read by scene dialogue (37468), `CommentOnPlayerAction` and combat grunts (0x8022B0 → SpeakSound): the local game choosing what the actor says | upkeep of a decision input | no: a copy's lines are its owner's, so it keeps `kCanSpeak` clear (as spawned) |
| 37752 | `Character::UpdateVoiceTimer` (ours; meh: `Character::sub_37752`) | Counts the voice line's timer (`Actor::voiceTimer` +0x110) down; at 0 marks it ended (`soundCallBackSet` 1, or 2 when talking to the player) | upkeep | **yes** |
| vfunc 0x40 = 37544 | `Actor::UpdateSoundCallback` (CL `UpdateSoundCallBack`) | Starts a queued line (`soundCallBackSet` 3, set by `SpeakSound` 37542 or its sound's callback): timer from the sound's duration, the face's expression, the subtitle. Ends an ended one: hides the subtitle, clears head tracking, the dialogue menu's topic when it is the speaker's | upkeep | **yes** |
| 39344, 37715, 39345, 29559, 19822, 37493 | `ActorProcess::IsDoingFavor` (meh), `Character::IsFollowingOrEscorting` (ours), `AIProcess::GetFavorPackage` (ours, high +0x358), `TESPackage::GetLocationPosition` (ours; IntuitiveDragonRideControl hooks it as `TESPackage_sub_140437ac0`), `TESObjectREFR::GetDistanceSquared` (ours), `Character::SetDoingFavor` (ours) | A follower doing a favour stops when the player is too far from the favour's spot | decision | no |
| 38011 | `Character::ShouldRotateToTrack` (ours; meh: `Character::sub_38011`) | Whether the body turns toward the headtracking target, from the package and target → `boolBits` `kShouldRotateToTrack` | decision | no (the sync turns copies) |
| 39942 | `AIProcess::UpdateSleepTimer` (ours) | A sleeping actor's wake timer → `boolFlags` | decision | no |
| 44302 | `GetGlobalTimeMultiplier` (meh) | VATS slow motion for the delta | helper | - |
| 39943 | `AIProcess::UpdateIdleTimers` (ours) | Idle timers: stops an idle (`StopCurrentIdle`, the "StartIdleAnimationDriven" check) when they run out | decision (idles are replayed from the owner) | no |
| 37410 | `Character::InitiateMovementBlockedPackage` (ours) | On `boolFlags` `kMovementBlocked`: a MovementBlocked package (type 36) | decision | no (and `HookPutCreatedPackage` refuses it) |
| 37227 | `Character::CalculateEssentialProtected` (po3, EssentialsBeGone; NoahBoddie's DDS hooks it too) | `boolFlags` `kEssential`/`kProtected` from the base NPC and the quest aliases holding the actor. Its only other caller is `Actor::KillImpl` | upkeep | **yes**, in high process as UpdateAI does |
| 39373, 39377 | `AIProcess::GetLipData`, `AIProcess::UpdateLipSyncGraph` (ours) | For a race without a FaceGen head (dragons): the phoneme graph variables (`TESRace::phonemeTargets`) from the line's lip data at 30 fps | upkeep | **yes**, in high process |
| 40413, 36310 | `PlayerCharacter::IsSleeping` (meh), `Calendar::GetTimeScale` (ours) | The catch-up delta for an actor below high process while the player sleeps or waits | helper | - |
| 68167, 68160, 13822 | scratch array init and free | | helper | - |
| 19816, mover vfuncs 15 and 8, 41298 | `TESObjectREFR::GetWorldSpace` (meh), `MovementControllerNPC` update, `ProcessLists::QueueActorMovementUpdate` (ours) | Below high process: the AI's movement controller update, and its pending path requests queued | decision (movement) | no (the sync positions copies) |
| 38785 | `Character::UpdateMagicAndRegen` (ours; MagicUtils hooks its regen calls) | Below high process: casters, `MagicTarget::UpdateActiveEffects`, regen | upkeep, not needed | no: high copies get it from `Job_Combat_and_magic`; a copy's values and effects are its owner's |
| 37401 | `Actor::EvaluatePackage` (CL; DragonWar has a commented-out hook) | Picks the package | decision | no |
| 37799, 39867 | `Actor::GetActorMover` (meh), `ActorProcess::HasRunOncePackage` (meh) | Results unused (debug leftovers) | helper | - |
| 39118, 36300, 11672 | `AIProcess::GetLastUpdateDay` (ours), `Calendar::GetGameDay` (meh), `ExtraDataList::RemoveInfoGeneralTopic` (ours, extra 0x4C) | On a new day, forgets the general topics said | decision (dialogue state) | no |
| 39197, 11726, 24505, 37668, 37380, 24525, 11943 | `AIProcess::GetEndAlarmOnActor` (ours, CL field `endAlarmOnActor`), `ExtraDataList::GetPlayerCrimeList` (ours, extra 0x34), `TESFaction::SetPlayerEnemy` (ours), `Character::ResetKnowledgeOfActor` (ours), `Actor::GetCrimeFaction` (meh), `TESFaction::sub_24525` (meh), `ExtraDataList::ClearPlayerCrimeList` (ours) | When the alarm on the player ends: the crimes this actor saw are dropped, its factions stop being the player's enemy, detection of the player resets | decision (crime, world state) | no |
| 29581, 39347, 39607, 12325, 37474 | `TESPackage::IsInterruptType` (ours), `AIProcess::IsTalkingToPC` (ours, CL field `talkingToPC`), `AIProcess::GetHighUnk464` (ours), `GetHandleUnsafe` (meh), `Actor::EndInterruptPackage` (CL) | Ends a DoNothing interrupt once the dialogue menu has another speaker | decision | no |
| 38303, 13894, vfunc 0x48, 37402, 37907 | `Actor::IsMountBeingRidden` (ours), `BGSDefaultObjectManager::GetSingleton` (CL), `HasKeywordHelper` (CL), `Actor::InitiateDoNothingPackage` (CL), `Actor::InitiateClearMountPositionPackage` (ours, type 43) | A mount being got on or off: a DoNothing or ClearMountPosition package | decision | no |
| 39108, 39109 | `AIProcess::UpdatePackageAndProcess` (ours), `AIProcess::UpdatePackageAndProcessTimed` (ours) | The AI proper (above); 39109 re-enters UpdateAI with 0 for low actors | decision | no |
| 11736 | `ExtraDataList::PruneRunOncePacks` (ours, extra 0x1B) | Drops run-once packages from another day | decision (package bookkeeping) | no |
| 39174 | `AIProcess::UpdateLowMountRider` (ours) | A low-process mount takes its low-process rider along or dismounts it | decision (movement) | no |
| 39116 | `AIProcess::ComputeLastTimeProcessed` (CL) | Dead actors: the process clock and a corpse timer | decision | no |
| 39132, 39131, vfunc 0xE5, 37612, 441532, 38469, 38455 | `AIProcess::GetEssentialDownTimer`/`UpdateEssentialDownTimer` (ours), `StopCombat` (CL), `Actor::SetLifeState` (CL), a `TESDataHandler` flag, `GetActorValueModifier`/`RestoreActorValue` (meh) | An essential actor gets up when its down timer ends | decision (life state is the owner's) | no |
| 40011, 36911 | `ActorProcess::sub_40011` (meh), `TaskQueueInterface::QueueKillActor` (ours, task 10) | A down actor that is no longer essential or protected is killed | decision | no |
| 19804, 13308, 36304, 37730, 41343 | `TESObjectREFR::Respawns` (meh), `TES::IsCellLoaded` (ours), `Calendar::GetHoursPassed` (ours), `Character::CanRespawn` (ours), `ProcessLists::AddToTempChangeList` (ours, sets `kInTempChangeList`) | A dead actor that respawns is queued for it once its cell is unloaded and the time has passed | decision (world state, the host's) | no |
| 37352 | `Character::UpdateCellAttachment` (ours) | Below high process: 3D and exterior cell follow whether the cell is attached | upkeep | **yes**, below high process |

CL: CommonLibSSE-NG. meh: meh321's AddressLibraryDatabase names. po3: powerof3's EssentialsBeGone
(`CalculateEssentialProtected`, hooked at UpdateAI +0x293 and in `KillImpl`). NoahBoddie's
death-defiance-system hooks 37227 and patches UpdateAI's essential check in the dead branch
(+0x96A). The search found no plugin, SkyMP or upstream Tilted Online code that calls UpdateAI's
upkeep for actors without AI: nobody else cuts it.

The four upkeep rows are what `HookUpdateAI` runs for copies. The voice line one is proven by
`copy-ends-voice-line`: without it, a copy queued its owner's greeting and never played it (the
scenario fails at "copy's line plays", control run 2026-10-12); with it, the copy's line plays and
ends within 0.15 s of its owner's. A line's state `soundCallBackSet` 3 with no sound behind it can
also rest after a line (the audio system's callback, `Character::SoundCallback_SetCallbackState`,
sets it by form id, ours); `UpdateSoundCallback` leaves that one alone, on owners as well (the
client's own player was seen resting at 3). Where one such 3 on a copy came from in
`move-host-npc` was not traced.

The same scenario found a regression from the bridge migration (36e2241d): `OnNotifySubtitle` played
the owner's subtitle text as a sound file on the copy (`ActorSpeakSound(text)`), which queued a line
with an empty sound over the real one. It shows the text again (`ActorShowSubtitle` →
`SubtitleManager::ShowSubtitle`), as before the migration.

## `Actor::Update` (37348): audited 2026-10-12

Run for every high actor after `UpdateAI` (vfunc 0xAD), copies included: a copy needs most of it. 100
callees in 0x12B1 bytes; audited by block, since its decisions sit in a few branches. It returns at once
without 3D or outside an attached cell; after a skip of 900 s or more it only sets the force-update flag.

| Block | What it does | Class | On a copy |
|---|---|---|---|
| Bleedout start | `boolFlags` kInBleedoutAnimation while essential-down or bleeding out: the default object 77 action | follow-through of the life state | the action is refused (`HookPerformAction`); the owner's bleedout is replayed |
| `Actor::UpdateTrespassing` (37578, ours) | only the player can trespass; any other actor's `kIsTrespassing` is cleared | player only | a remote player's copy never trespasses on the other game: crime is the owner's, undecided in the contract |
| `Character::UpdateSprintStamina` (38022, ours) | sprint drains stamina, for the player or the player's mount | player only | nothing |
| Character controller | LOD settings from the camera; while attacking with animation-driven movement, the target's controller | upkeep | runs |
| Face, attack sound, light | blinking/eyes by sit state, the weapon's attack loop sound, the actor's light | upkeep | runs |
| **Sneak** | decides whether the actor should sneak: its combat controller in combat, `forceSneak` (ActorState2 bit 2), its package's always-sneak flag, or a teammate (`kPlayerTeammate`) while the player sneaks and is followed (40492). If its sneaking flag differs, it clears the middle process's movement flags (37592) and performs the sneak toggle | decision | a copy has none of those reasons, so while its owner sneaks it tries to stand up every frame. The toggle is an action (0x13007), which `HookPerformAction` refuses: 86 refusals in 1.4 s of sneaking (probe, 2026-10-12). The copy keeps its owner's state: `sneak-host-player` and `sneak-client-player` hold it for 1 s |
| Water | submerged level → controller swim flag, `kInWater`, `kUnderwater`, time underwater; entering or leaving water: swim state, sheathe, torch put away, splash sound and ripples | upkeep, with follow-through | the state runs (the copy is where its owner is); equipment changes go through the equip hooks, which refuse them on copies |
| Drowning | a head that was under for `fActorLeaveWaterBreathTimer` (4 s) gasps when it surfaces (`AIProcess::QueueSayComment` (7, 102), ours, every actor in high process, the player too); NPCs out of breath get a Surface package (type 29); the player out of breath takes damage and plays a drowning sound | decision | the package is refused (`HookPutCreatedPackage`). The gasp: the owner's line arrives over `NotifyDialogue` while the copy, a little behind, is still under water; when the copy surfaces it is talking, and `QueueSayComment` then only appends to the high process's deferred comments (+0x240), which nothing a copy runs plays. A copy not talking would say it at once (0x6E1A10: the story manager picks a line, no `kCanSpeak` test), so a copy that surfaced before its owner's line arrived could gasp on its own. `dive-host-player` / `dive-client-player`: the owner dives 6 s and surfaces; the copy plays exactly the owner's gasp (same 1578 ms line), keeps the do-nothing package and its owner's health (6 of 6, 2026-10-12) |
| Harmful water | a cell's water damage per second through `Actor::DoDamage` (37335) | decision (damage) | `HookDamageActor` drops damage to a copy that does not come from its owner |
| Dying | life state 1: stop movement, face, `TESObjectREFR::sub_14630` | follow-through | runs on the owner's synced life state |
| Dead | resets the greeting timer | bookkeeping | harmless |
| `Actor::UpdateCombatSearchState` (37273, ours) | in combat: caches the combat target and `kSearchingInCombat` | decision bookkeeping | a copy is not in combat (no AI); clears both |
| Pending hands | weapon state 1 or 4 (ActorState2 bits 5-7): `DrawWeaponMagicHands` (vfunc 0xA6) again | follow-through | retried, as for any actor; with the AI cut no decision leaves a copy at 4 |

So `Actor::Update` needs no copy hook of its own: every decision in it acts through a function an
engine-level guard already holds (actions, created packages, damage, equipment). The copy contract's
third principle, guarding the engine's functions rather than the callers, covers it. The sneak toggle
is the cost: a refused action built each frame while a copy sneaks.

## The cut

Copies return at the entry of `Character::UpdateAI` (`HookUpdateAI`, Actor.cpp). That is the engine's
own `kProcessMe` early-out applied to the AI alone; every body job above keeps running. The hook still
runs UpdateAI's upkeep for copies (the table above), under `UpdateAI`'s own conditions, and
A copy holds the engine's do-nothing package (type 27, `Actor::InitiateDoNothingPackage` 37402): whenever
the engine asks for a copy's package (`checkForNewPackage` 39114: no package unseen, a process level
change, a scene or dialogue ending), `HookCheckForNewPackage` gives it that one, and `HookPutCreatedPackage`
lets it through. So `ShouldOnlyCheckPackage` is false for a copy as for any NPC with a package, and an
unseen copy gets its upkeep too. This replaced a hook on 37277 that returned false for copies (2026-10-12).
The package never runs: UpdateAI, where packages are picked and run, returns for copies.
The guest's own game still put a follower alias's combat override (`PlayerFollowerCombatOverridePackage` 5C851) in a
copy's running slot after a fight, with no message from the host (whose follower was back on `PlayerFollowerPackage`
5C84B): `save-load-gate` read the copy on 5C851 on two profiles (v1-mod-profiles, 2026-10-29). Which call did it was not
caught. `HookInitFromPackage` (38959, the call every package put in a slot goes through) now keeps a copy's running
slot on its owner's package once known (`CopyPackage::Put`, ClientTests `[CopyPackage]`) and logs "keeps its owner's
package" when it overrides one, naming what the engine wanted.
`PutCreatedPackage` (37570) also skips its "stop interacting" step for types 26-28, so it does not stand a
seated copy up. Checked: every copy holds type 27 (`copy-ends-voice-line` asserts it), and the full suite
gives 33 pass / 6 fail, every failure one listed under "Still open" or the baseline's. The unseen case
itself was not reproduced in QASmoke: at 1000 units the copy counts as seen, and at 4000 units the owner's
line is never queued on the copy at all, with or without the package (callback -1, no sound; not traced). Without the cell attachment, a copy left behind when the
party changed cells kept its 3D and stayed loaded with no owner (Gerdur after `party-changes-cells`,
the regression the reverted attempt also showed), and every later baseline reset failed.
`HookApplyMovement` (37356, previously misnamed `HookActorProcess`) still skips the movement
integration, since the sync positions copies.

`ProcessLists::UpdateHighActor` runs on the job threads, several actors at once, so the hook must not
touch shared state without a lock (a debugging map once hung the game there).

### What the AI used to repair

Cutting the AI showed one of our own bugs it had been hiding. `OnCharacterSpawn` put every copy in
the local player's cell at its owner's position. The host's answer to an `EnterCellRequest` can
arrive after the player left again (`party-changes-cells` goes to Riverwood and straight back):
the Riverwood copies then stood in QASmoke at their Riverwood coordinates. With AI, their packages
moved them back to Riverwood, out of the player's sight; as puppets they stayed, owned by nobody,
and every later baseline reset failed. Logged on 2026-10-11: the spawns at 23:18:21.554, 32 ms after
the client arrived in QASmoke, then Sven's parent cell 9731 → 32AE7 (QASmoke). Neither
`UpdateCellAttachment` nor `ForcePosition` changed it. A copy of a reference from the game's data now
goes to its owner's cell (`NotifyCharacterSpawn::CellId`). Forcing the race (both games go to
Riverwood and back after 0.25 s): before, the round whose 24 Riverwood spawns came late left 4 copies
in QASmoke; after, the round with 24 late spawns left none, in 12 rounds. Actors created for a spawn
(players, temporary NPCs) still start at the player: no case of theirs was seen.

What a copy still gets from the engine:
- `Actor::Update` (37348): audited below ("`Actor::Update`"). Body upkeep, plus four decisions that
  the engine-level guards already stop on copies.
- Detection of others (`Job_Detection`; `HookUpdateDetectionState` covers copies), combat groups:
  "audit when touched" in the contract.

Rejected: `EnableAI(false)` / `kProcessMe` (stops the graph job), `SetPlayerControls` (the same plus
controls-driven movement), keeping the AI and filtering its effects (SkyMP overrides
`DrawWeaponMagicHands`' argument, `reference/plugins/skymp/skyrim-platform/.../FridaHooks.cpp`; the
reverted attempt dropped sheathe calls).

## How a replayed action reaches a copy's graph

The owner's `HookPerformAction` records each action its engine **accepted**, with the graph
variables just before it (a refused action is not sent; the player's input handlers retry every frame
until the graph takes them). On the copy, `AnimationSystem::Update` replays it at the copy's time ([copy-clock.md](../../design/copy-clock.md)) with
`ActorMediator::ForceAction` → `PerformComplexAction` (38953) → `NotifyAnimationGraph` (32901) →
`BSAnimationGraphManager::NotifyAllGraphs` (63362). That sends the event to every graph of the actor
and succeeds if any state machine has a transition for it from its current state. A refusal means
the copy's graph is not in the state the owner's was in.

Behaviour files can be read with `scripts/re/behavior_dump.py` (BSA extraction, state machines with
event names and conditions, writers of a variable). The magic chain, from `magicbehavior.hkx` and
`0_master.hkx`:
- `MagicRootBehavior` goes `MagicReadyState` → `CastingState` only on `BeginCastRight`/`BeginCastLeft`.
  `MRh_ReadyCast_Behavior [MRh_Ready_State]` takes `MRh_SpellAimedStart` and the like only inside
  `CastingState`. `CastStop` returns to `MagicReadyState`.
- Nothing in the exe or the ESM sends `BeginCastRight`. `BeginCast_OnFalseToTrueMod`, a
  `BSEventOnFalseToTrueModifier` on `MagicRoot_MG`, raises it when `bWantCastRight` goes false →
  true. The engine sets that variable in `Actor::SetWantCast` (37950), from `ActorMagicCaster`
  `RequestCastImpl`/`FinishCastImpl` (34136/34141). The modifier compares its bound member with
  the value from its previous update (63226), so it only sees changes the graph's update processes.
- `CastStop_EventOnFalseToTrueMod` raises `CastStop` when `NotCasting` goes false → true, and
  `NotCasting` is written by the graph itself (`InvertNotCasting_IsActiveModifier`).

The refusals measured on 2026-10-11 (DIAG traces of the copy's active state machines next to the
owner's, since removed) had four causes, each fixed where it starts:

1. **Variables from the future.** Interpolation applied the *next* snapshot's variables
   (`second.Variables`), up to 100 ms ahead of the playback time, every frame. Edges landed early
   (while the copy was still equipping, so the modifier never fired), and an action's newer variables
   were overwritten by an older snapshot. Now the copy's graph holds the newest variables by owner
   tick among the current snapshot (`first`) and the replayed actions (`RemoteAnimationComponent::
   Variables`). In 20 refused casts the bool had already gone true before the action; 15 of those
   were refused.
2. **Variables and event in the same instant.** The owner's graph had processed the variables in an
   update before it accepted the action. The copy loaded the head action's variables one frame
   before playing it; since 2026-10-13 both go in from the copy's own graph update (next section).
3. **Variables the graph writes itself, forced from the owner.** `currentDefaultState` is
   `Default_Behavior`'s `syncVariable` (its state id). The root's `JumpLandEnd` reads it to return to
   the state it left, so the owner's value made a copy that equipped while landing drop its hands.
   `NotCasting` forced false on a copy that was not casting yet flipped every update and raised
   `CastStop` each time. Both are no longer synced (`AnimationGraphDescriptor_Master_Behavior`).
   The owner's values also come from its active graph, which is the first-person one for a
   first-person player.
4. **Throttled graph updates off-screen.** `Actor::ModifyAnimationUpdateData` (38054) marks the
   update not visible for a culled actor, and its graph is then updated only now and then. A copy
   behind the camera held `bWantCastRight` true in its variable set for 11 frames before the
   modifier's member saw it. Copies set `kForceAnimGraphUpdate` (`boolFlags` 1<<29, CommonLib)
   every frame, as Precision does for the actors whose ragdoll it drives.

Measured with the magic/combat/hands scenarios twice: refused cast starts went from 11-15 per run to 3.

## Replay in step with the graph (2026-10-13)

The one-frame lead of fix 2 was not enough: run alone, `firebolt-client-hits-host-npc` still failed
about half the time on a refused `MRh_SpellAimedStart`, and the magic scenarios run back to back
left the client's copy on `Magic_Equip`. Traced with per-frame dumps of the copy's active states
(the names in its `AnimationGraphState` snapshot) next to the owner's:

- On the owner the cast is one causal chain inside its graph update: the input sets
  `bWantCastRight`, `BeginCast_OnFalseToTrueMod` raises `BeginCastRight`, the graph enters
  `CastingState`, and the caster performs `MRh_SpellAimedStart`.
- On the copy, our update (on a job thread inside `HookVMUpdate`) and the copy's graph update
  (`Job_Actor_animation` → `RunOneActorAnimationUpdateJob` 41453 → `TESObjectREFR::sub_20123`,
  "UpdateAnimationGraphs", which calls `UpdateAnimationGraphManager` 32899) are not ordered. A
  variable load "one frame before" was sometimes seen by zero graph updates before the action
  (`CastingState` showed up one frame after the refusal).
- Two of our frames instead was worse (8 of 8 failed): without a cast, `CastingState` raises
  `CastStop` (`NotCasting` false → true) about one graph update after it is entered, so the action
  has to land within one update of the transition.
- The copy's own caster never performs the action (nothing requested a cast on it), so the copy
  cannot be made event-driven that way.

So the replay now runs from the copy's graph update. `AnimationSystem` stages the head action on the
copy's `ActorExtension::Replay` (atomic phase); `HookUpdateAnimationGraphs` (20123) loads its variables
at the start of one graph update and plays the action at the start of the next; our update takes the
outcome a frame later. A copy whose graph nobody updates (no animation job) gets the step from our
update after 10 frames, logged.

A copy whose equip finished in the same graph update as the variable load still refused (1 in 8):
the owner's cast request had come only after its graph was ready, because the player's input
handlers retry every frame until the graph takes them. The step does the same, bounded: a refused
action is tried again at the start of each of the next 3 graph updates ("took replayed action ... N
graph updates late", debug).

Measured: `firebolt-client-hits-host-npc` 8 of 8 with no refusal (7 taken at once, 1 after 2
retries); the magic, hands and combat tags run back to back twice pass apart from the kill-credit
scenarios (fixed below). The host relay also stamps a snapshot with the owner's tick now
(`HostCharacters::OnMovement`), the time base the owner's actions carry, instead of the host's
receive time.

## A copy's casts are free (2026-10-13)

A copy spent its own magicka on the casts it replays (1.2-2.4 below a god-mode owner in every
firebolt/flames run). `ActorMagicCaster` spends in three places: `Update` (34143) while charging
(`UsesResourceWhileCharging`, accumulated in `fCostCharged` 0xF0) or channelling (state 6),
`InterruptCastImpl` (34140, the charge so far), and `SpellCast` (34144, on release; our hook already
skips it for copies). `uiFlags` (0xF8) bit 1, CommonLib `kSkipCheckCast`, is the engine's free cast
(`SetSkipCheckCast`, vfunc 0x13): `CheckCast` (34145) returns true without checks and `Update` spends
nothing; `FinishCastImpl` and `InterruptCastImpl` clear it. These two are its only readers in the
magic code.

Copies set it in both readers (`HookCheckCast`, `HookCasterUpdate`): a copy's cast is its owner's,
checked and paid for there. Setting it at the charge start was not enough: the copy's
`CastSpellImmediate` (from `NotifySpellCast`) runs `FindTargets` (34410), which puts a concentration
caster into state 6 with no check, and one frame of Flames was spent before the next check. Measured
with the owner in god mode: gap 0.0 in 3 runs (0.1 with the charge-start hook, 1.2-2.4 without).

The owner's health, magicka and stamina now go out within 100 ms of a change (`RunPoolUpdates`,
health alone before): a copy that spends nothing shows its owner's value only as fast as it is sent.

## A copy's death (2026-10-13)

`death-host-npc` and the kill-credit scenarios failed on the copy's last action: the owner's
`DeathAnimation` (or `RagdollInstant`), recorded with the dying life state, was refused on the copy.
Two things killed the copy before its replay:

- `NotifyDeathStateChange` ran `Actor::Kill` → `KillImpl(nullptr, 100, true, true)` at once, an
  instant ragdoll 300 ms before the owner's death action replayed.
- The owner's health reached the copy first (100 ms path) and at 0 the copy's engine killed it
  itself. `KillImpl` (37896) performs the death action through `PerformAction`; on a copy our hook
  refuses the copy's own actions, so it fell back to a ragdoll.

Now the copy dies on its owner's timeline. When the replay reaches an action its owner took while
dying (`ActorState` life state 1 in the action's `State1`) and the copy is alive,
`Actor::KillWithDeathAction` runs `KillImpl` with the copy's actions forced, on the same branch as
the owner's (`ragdollInstant` when the owner's action was `RagdollInstant`), and the action counts as
taken if `KillImpl` performed it. A copy is in deferred kill (below), and the death-state
message applies 500 ms later, only to a copy still alive (an owner ragdolled to death with no action).
Measured: `death-host-npc`, `client-spell-kill-credit`, `client-melee-kill-credit` 3 of 3 each.

`client-melee-kill-credit` also had a test bug: a second `attack()` 0.5 s after the first passed on
the first swing's `IsAttacking`; it now waits for the last swing to end.

## A hit forwarded to the owner (2026-10-13)

A hit on a copy is only a request: the attacker's game sends the damage (difficulty applied,
`CalculateRealDamage`) and its `DoDamage` hook (37335) applies nothing to the copy. The owner takes it
as the engine's own hits do:

- `DoDamage` → `Actor::SetActorValueModifiers` (38468, modifier 2 = damage, health, -damage,
  attacker) → `Actor::HandleHealthDamage` (38606, vtable 0x104) with the attacker → at health ≤ 0
  `Character::sub_38631` (bleedout for essential/protected, else) → `Kill_impl2` (37316), which
  stores the killer handle (Actor+0x108) and queues the kill (`TaskQueueInterface::QueueKillActor`);
  a second call while a kill is queued (process flag, `ActorProcess::sub_40011`) does nothing.
  `HandleHealthDamage` itself kills with `KillImpl(attacker, damage, true, ragdollInstant = false)`:
  a death animation.
- The owner applied it with `ForceActorValue` (`RestoreActorValue`: the same path, no attacker), so
  the engine queued a kill without a killer, and our `KillImpl(killer, 100, true, true)` then killed
  with an instant ragdoll: the body was thrown (~70 units up, ~120 sideways) and the copy, alive
  until the owner's `RagdollInstant` replayed 300 ms later, was dragged along by interpolation.
- Now `Actor::DamageHealth` calls 38468 with the attacker. Measured: the killer is recorded at the
  death, both games play `DeathAnimation`, and the copy comes to rest within 50 units of its owner's
  body (10 of 10 melee and spell kills).

### A copy is in deferred kill

A copy shows its owner's health as is, at or below 0 too, and never dies from it: `HookUpdateAI`
puts every living copy in deferred kill each update (`Character::StartDeferredKill` 38616 with the
papyrus wrapper's `true`; it lives on the middle/high process, so a copy that unloads gets it again).
This is skymp's way (`skymp5-client` `formView.ts`: `startDeferredKill()` on every remote actor,
`endDeferredKill()` then `kill()` on the server's death). In deferred kill `HandleHealthDamage`'s
bleedout/kill check and `KillImpl` only mark a pending kill; `Actor::Kill` and `KillWithDeathAction`
end it first (`EndDeferredKill` 38618 queues the pending kill on the task queue, so the kill made
right after wins). A kill move's `KillActor` event (`KillActorHandler::Handle` 42838) ends only a
deferral started with `false`, so a kill move on a copy does not kill it either. Replaced a 0.01
health floor and a map undoing it after the death. Measured: the copy shows -2.04 while alive and
dies with its owner's `DeathAnimation` 0.36 s later; kill-credit, `death-host-npc`, `health-host-npc`
3 of 3 each.

Checked in review (2026-10-13): `StartDeferredKill` is safe each frame (`ActorProcess::StartDeferredKill`
40033 sets the state once, ORs the flag, resets the 15 s timer). It needs the middle/high process data,
and `MoveToLow`/`MoveToMiddleLow` end it: a copy far enough to drop to low process can die from its own
health (unseen at that distance). `EndDeferredKill` re-runs a held kill with the *local player* as the
killer (`sub_140715AE0` → `Kill_impl2(actor, player)`); on a copy the immediate `KillImpl` after it wins,
and the copy's recorded killer stayed 0 (host `kill` and a health-driven death, both checked).

## Actions before the owner has an id (2026-10-13)

The owner's actions reach the network through its `LocalAnimationComponent`, which was added only
when the host answered the assignment. An NPC placed with `placeatme` falls at once (`JumpFall`,
`JumpLand`), before the answer, so those actions were lost: its copy started in the graph state from
the assignment (in the air) and never landed (`firebolt-client-hits-host-npc` failed on Lydia's copy
having no action against her `JumpLand`). The component now comes with the assignment request, right
after the snapshot that carries the graph state, so the actions queue in order behind it and go out
with the first update once the id arrives (dropped if the answer gives the actor to someone else). Since
2026-10-23 there is no answer: the actor has its id as it is announced.

## An action's target (2026-10-13)

An action carries its target (`TESActionData::target`): the victim of a paired idle such as a kill
move, the furniture of a sit. The owner recorded it as its own form id, which a copy's game looked up as
its own: the host's `pa_KillMove1HMWolfB` targeted the wolf `FF001985`, nothing on the client, and the
wolf's half targeted `14`, the client's own player. An actor target now crosses as its session id + 1
(`ActionEvent::TargetActor`, 0 none; session ids are EnTT entity ids and use all 32 bits, so they cannot
share `TargetId`); other references keep their form id, the same in both games, and a temporary one is
dropped (`AnimationSystem::TargetToNetwork` / `TargetFromNetwork`, on the game thread when the step is
staged; the replay itself runs in the copy's animation job).

With the right target the copies are paired by the engine: the host's character's copy takes
`pa_KillMove1HMWolfB` and the wolf's copy `KillMove1HMWolfB` in the same frame, both get the kill-move
flag (`Actor::IsInKillMove` 38615) and `bIsSynced`, and the client's own player is untouched. The wolf's
copy's half sends `KillActor` too, but a copy is in deferred kill: it dies only with its owner's death
action, ~150 ms later. Measured with the animation trace (`scenario host-kill-move`).

## Kill cams slow the clock (2026-10-13)

Every kill move plays a kill cam (`VATS::mode` 4; `fKillCamBaseOdds` 0.1 and the level bias decide only
other kills' cams, `sub_140419340`). The kill cam's update (`sub_1407F22B0`) passes its camera shot's
`globalTimeMult` (`BGSCameraShot` data at 0x58, the multiplier at 0x70) to
`BSTimer::SetGlobalTimeMultiplier` (68245) and ramps it back to 1; `VATS::SetMode(0)` resets it. Some
shots slow the clock to ~0.25: the whole game, every NPC, runs slower, while the other games do not.
On the host the kill move took 4.1 s against the copy's 2.9 s. The clock is now shared: the hook
(`BSTimer.cpp`) keeps what this game's engine asks for, sends it to the host (`TimeScaleRequest`), and
runs the clock at the slower of it and the other players' slowest (`NotifyTimeScale`, from
`HostTimeScale`). Slow Time (`VATS::SetMagicTimeSlowdown`), bow zoom (`BowZoomStartHandler`) and
`sgtm` use the same call, so they slow everyone too. The engine's own per-actor multipliers
(`playerTimeMult`, Slow Time's faster player) stay local.

## Kill moves by the client (2026-10-13)

A kill move is one action on the attacker (`pa_KillMove…`) that the engine also performs on the victim
(`TESActionData::targetEventName`, the victim's half). Three things kept the client's from playing out:

- **The victim's half was refused.** On the client the victim is a copy, and `HookPerformAction` refused
  every action on a copy not replayed, so the engine fell back to a normal swing. `ShouldAttackKill`
  (38559) passes without combat; the harness's `idle_conditions` showed every condition true. A copy's
  action aimed at the local player during the player's own action is now taken, never sent.
- **The host's pair waited ~3.1 s.** A paired animation starts once every half is at its mark
  (`BGSSynchronizedAnimationInstance` 32794 tests `BSSynchronizedClipGenerator::doneReorientingSupportChar`
  at 0x12A of each participant; the engine's debug text calls it "(At Mark)"). A generator's update
  (63050) sets it when its `getToMarkProgress` is 1 and the actor's world transform matches the target the
  instance laid out from the lead (0xB0; translation, rotation and scale within a small epsilon,
  `sub_140A8E0A0`). The wolf's half leads (`quadrupedbehavior.hkx` `KillMove1HMWolfA/B`: lead 1,
  `getToMarkTime` 0, `markErrorThreshold` 0.1); the player's half supports (`1hm_behavior.hkx`, lead 0,
  reorient 1). A real supporting actor is moved onto its mark by its own movement (root motion); a copy
  never moves itself and is placed where its owner stands, ~1 unit off the host's mark (positions travel
  quantized, its owner's wolf is a copy), so it never matched and the pair waited out a fallback (~3.1 s,
  not traced; every variant tried started then). Ruled out by measurement: the copy's forced
  `bIsSynced` (a graph output; dropping it from the sync changed nothing), its position, z and rotation
  sync (skipping each during the kill move changed nothing), the character controller (on the ground
  throughout). Now a copy's half counts as at its mark (`HookUpdateSynchronizedClip` in
  `Animation.cpp`): its owner's game placed it. The host's pair starts ~115 ms after the action (the
  client's own ~150 ms).
- **The kill credit went to the host's player.** The client's blows inside the kill move (`HitFrame`) are
  forwarded hits. The kill move holds its victim in a deferred kill (not scripted), so the hit only marks a
  pending kill; at `KillActor`, `KillActorHandler::Handle` (42838) first ends the deferral
  (`EndDeferredKill` 38618 → `sub_140715AE0`), which queues the pending kill with `g_thePlayer` as the
  killer, then calls `Kill_impl2(victim, partner)`, which does nothing once a kill is queued. The owner now
  does not apply a forwarded hit to an actor held by a kill move (`Actor::IsHeldByKillMove`:
  `IsInKillMove` 38615 and `Character` 38619, the test `KillActorHandler` makes); the kill move kills it
  and credits its partner, the client's character.

The wolf's copy on the client takes its `KillActor` (a copy's deferred kill is the scripted kind, which
`KillActorHandler` does not end) and dies with its owner's death action ~0.6 s later: the host's pair
starts ~0.35 s after the client's, and the death action takes the interpolation delay.

## A copy's spawn: its owner's graph state

A copy used to start by replaying its owner's last action. A fresh graph refuses any action that
needs a state it is not in: `Unequip` (it is already sheathed), `JumpLand` (it never jumped), a
furniture idle (`IdleChairFrontEnter`), a ready spell. The checker then saw the copy's last action as
0 against its owner's, and a seated NPC stood on the other game.

The engine has the state itself, for savegames: `TESObjectREFR::SaveAnimation` (20098, our name;
change flag 1<<28) → `IAnimationGraphManagerHolder::SaveAnimationGraphs` (32879) →
`BSAnimationGraphManager::SaveGraphStates` (63368) → `BShkbAnimationGraph::SaveState` (63601), which
writes an hkbCharacter snapshot (active states, variables, clips) through an `AnimationStream`; the
load side is 32881 → 63369 → `BShkbAnimationGraph::LoadState` (63602). Nobody had published the
stream: goonJungus/skyrim-cpp has empty stubs, and no plugin calls these.

Where the engine itself uses it (IDA, 2026-10-12):
- **Savegames.** `TESObjectREFR::CheckSaveGame` (19527) keeps the animation change flag for a reference
  with 3D whose `ShouldSaveAnimationOnSaving` (vfunc 0x7B; actors override it) says so; loading the save
  runs `LoadState` on the fresh graphs. This is why a seated NPC is still seated after a load.
- **3D unloading, objects only.** `TESObjectREFR::Set3D` (0x2e47c0) saves the state into the reference's
  extra data (`0x6258c0`, "BGSLoadGameSubBuffer::SaveAnimation" in its error string) when
  `ShouldSaveAnimationOnUnloading` (vfunc 0x7A) is true: doors, animated activators. Actors return false
  (CommonLib), so an actor whose 3D reloads gets a fresh graph and its AI puts it back in place.
- **Not on cell changes.** The player keeps its 3D and graph through `coc`, doors and fast travel; NPCs
  of the old cell drop theirs, NPCs of the new one start fresh and their AI rebuilds the pose.

A copy is the case the engine never has: a fresh graph, an owner with a history, and no AI to rebuild
the pose. So it takes the savegame path at creation. The initial load runs once per copy. The v1 follow-up also recovers refused unpaired actions
from their owner's captured result
([evidence and limits](animation-replay-recovery.md)); ordinary state changes while a copy is loaded go through
replayed actions, and scenarios have to cover them live (`furniture-host-player`/`-client-player`:
the owner uses QASmoke's cooking spit and leaves it, the loaded copy follows both ways, 2026-10-13).

`AnimationGraphState` (Games/Skyrim/Havok) implements the stream over a byte buffer (12 write slots,
14 read slots, the same sizes and byte order as `AnimationStreamSaveGame`/`LoadGame`) and saves each
graph by project name (`BShkbAnimationGraph::projectName`, 0x1F0): a player has `DefaultMale` and
`FirstPerson`, its copy only the first, and the manager's own load matches graphs by count and
order, which would skip both. The owner's state (about 7.5 KB per graph) goes with every spawn
(`AssignCharacterRequest`, `NotifyCharacterSpawn`, and `EnterCellRequest`, so the host's record of a
player is not the one from when it joined). The copy loads it once its graph is ready
(`AnimationSystem`), and the spawn action then counts as taken instead of being replayed. If the
load fails, the action is replayed as before (logged).

The engine keeps the actor's state flags (`ActorState` flags1/flags2: weapon state, sitting, life
state) beside the graph, so they travel first in the same data and are set with the graph: a graph
loaded alone showed drawn hands on an actor whose flags said sheathed, and the hands convergence drew
again (`Magic_Equip` as the copy's last action, then refused casts). The load waits for the 3D set-up
(inventory, factions) and the action queue waits for the load, so nothing lands on top of it.

Measured 2026-10-12: `host-kicks-client` after `draw-host-player` (2 of 2 failures before),
`party-changes-cells` and `load-door-not-replayed` pass; every load read all its bytes;
`burning-shows-on-copy` right after a hot reload passed 3 of 3 with the load and failed 3 of 3 without.
Full suite on the same games: 30 pass both with this session's changes and with them switched off,
but the sets differ. Fixed here: `copy-ends-voice-line`, `burning-shows-on-copy`,
`load-door-not-replayed` (the spawn refusal). Failing in both: `late-join-client` (party panel after
rejoin), `death-host-npc`, `client-spell-kill-credit`, `client-melee-kill-credit`, and the magic chain
run in sequence (`firebolt`/`flames`/`burning`: the client's copy left on `Magic_Equip`, its spell then
missing from the copy's equipment in `load-door-not-replayed` and `weather-follows-host`).

**Copies are respawned on travel, not moved (decided 2026-10-13).** Moving a player's copy through a
door with its owner (keeping its graph through the 3D reload: `Set3D` saves into `ExtraSavedAnimation`,
extra type 0x41, only with change flag 1<<28 and vfunc 0x7A true) was considered and dropped. A load
screen ends every animation in progress; only stances carry over (hands out, sneaking), and the
spawn's graph state already covers them. The spawn path stays for NPC copies (seated, leaning,
asleep, at a workstation) and joins either way, so moving would add a second path to save ~7.5 KB per
arrival. SkyMP also respawns on a cell change and corrects only flags (weapon drawn, sneaking,
blocking, dead) from each movement packet; that covers no furniture or idles. The host's record of a
player is refreshed from its `EnterCellRequest` so a respawned copy has its current gear.

Two regressions from the bridge migration (36e2241d) showed up on the way: when the server confirmed
an actor as ours, `LatestAnimation` was cleared instead of `LatestWeapEquipAnimation` (the owner then
reported no last action while its copies had one), and `OnNotifySubtitle` played the subtitle as a
sound (above). `both_go` in the scenarios waited only for "each game shows the other player", which
holds before the `coc` takes effect; it now waits for each game to arrive first.

## A copy's equipment: its owner's only (2026-10-21)

`EquipHook` refuses any equip on a copy outside `ScopedEquipOverride` (ours: an owner's equipment message, the
snapshot reconcile, a spawn's inventory), and `UnEquipHook` any unequip outside it but an item leaving the copy. An
item leaving an NPC makes the engine pick a weapon for it (inside `RemoveItem` 19689: seen as an equip of the best
weapon it carries and an unequip of the one in hand). Our `HookRemoveInventoryItem` used to hold `ScopedEquipOverride`
for the unequip, which let that pick through: a script taking the sword from the guest's copy put Halldir's Staff in
its hand, its owner holding an axe, and the worn-set reconcile (every 3 s) only sometimes ran before the scenario's
desync check. It now holds `ScopedItemRemovalOverride`, which `UnEquipHook` accepts and `EquipHook` does not.
`sword-box-asks-client` carries the staff and checks the copy never equips it (control 2026-10-21: with the old
override it fails at that step). Copies' equips log one line each ("Copy X equip Y: ours" or "the engine's, refused").

## Combat state and its readers (2026-10-20)

Read in IDA for AI state on copies (`docs/design/copy-contract.md`, "AI state"). Names: CommonLibSSE-NG and
meh321 where they have one, ours otherwise.

- **`Actor::IsInCombat` (38562, vfunc 0xE3 in IDA's SE numbering, CommonLib's AE 0xE5):** true when
  `combatController` (`+0x160`) is set and its stopped byte (`+0x43`) is clear. Nothing else.
- **`Actor::GetCombatState` (38556):** 0 when not in combat; otherwise 2 if the combat group (vfunc 0xD4) has
  a search state (`+0xD8`), else 1. Open Animation Replacer calls it for its `IsCombatState` condition.
- **Combat controllers run outside `UpdateAI`.** `CombatUpdateJob` (46902) calls `Actor::UpdateCombat`
  (38565, vfunc 0xE4) for one actor; 46903 queues it for every member of every combat group (members at
  group `+0x20`, count `+0x30`). `UpdateCombat` updates the controller, its target and the behaviour tree.
  So an actor with a controller in a group gets combat AI whatever `HookUpdateAI` does: a copy must never be
  given either.
- **`IsInCombat`'s callers:** 127 virtual call sites in 100 functions (a scan for `call [reg+718h]`; the function is
  reached only through the Actor and Character vtables, never called directly). 22 of them also touch a `+0x160`
  field (the combat controller when the object is an actor). A hook answering "in combat" for a copy with no
  controller must not reach one that then uses the controller.
- **The audit of those 22 (2026-10-22, IDA):**
  - **Use the controller after a yes, unchecked** (a copy answering yes would crash there): `Actor::Update` (37348,
    reads controller `+0x34`), `Actor::CheckValidTarget` (38560, `+0x42`), 38627 (a hit's reaction: passes the
    controller to 0x140559260, which dereferences it), 41261 (the count of combat targeting the player: `+0x8` for
    the barter menu's target).
  - **Start or stop combat** on a yes: `Actor::StartCombat` (38561: in combat after it, the actor leaves its scene,
    `GetCurrentScene` then 0x1403A0D20), `Actor::UpdateCombat` (38565), `Actor::StopCombat` (38566, three calls).
  - **Check the controller before using it, or touch another object's `+0x160`** (safe): the `GetShouldAttack`
    condition (21471, checks it), `Actor::PickUpObject`, `Actor::RemoveItem`, `TESObjectREFR::RemoveItem`
    (the UI's `+0x160`), 37239, `Actor::CalculateDetection` (checks both actors' and reads a worldspace's),
    `fUnk_Attacked` (38626, checks it), 0x1406F10D0 and 0x1406F20A0 (check it), 0x14071C020 (checks it), `Projectile`
    construction (the projectile's own `+0x160`); `TESNPC::Activate`, 0x140464EC0 and 42571 do not read it;
    `PlayerCharacter` 37273 runs for the player only.
  - A first hook answered a copy's owner's state everywhere but at those 9 call sites: the client crashed casting
    (`MagicCaster` 34450 → `Actor::EngagingInCombat` 38563, then the controller at `+0x160`). `EngagingInCombat` wraps
    `IsInCombat` for 23 more functions (weapon fire, spell casting, 37410, 39753, combat code), outside a scan of
    direct callers. So `HookIsInCombat` and `HookGetCombatState` (`Games/Skyrim/CopyAiState.cpp`) answer the owner's
    state only to the condition functions (21576 and 21740, by return address: Address Library ID plus offset) and to
    plugins (callers outside the game's image); every engine caller gets the copy's own answer (no controller: no).
    Papyrus `IsInCombat` (54945) and `GetCombatState` (54679) tail-jump from the VM's call and get the copy's own.
- **A player's own combat flag:** `PlayerCharacter::IsInCombat` (41024) reads `+0xBE3` bit 0x20.
  `PlayerCharacter::UpdateInCombat` (41238, ours) sets it each update when 41261 (ours,
  `CombatManager::CountGroupsTargetingPlayer`) counts a combat group targeting the player (the members in
  the high dword), and clears it otherwise, with a regen delay. 41261 also adds every enemy the player
  detects to the player's own combat group (`+0x9D8`). On a guest the host's NPCs are copies with no combat
  groups, so the count was 0 and the guest's player never in combat. Since 2026-10-22 `HookCountCombatTargetingPlayer`
  adds the copies whose owner's AI state is in combat with this game's player (`AiStateService`).
- **The combat target** (`Actor::currentCombatTarget`, `+0x104` in 1.6.1170, CommonLib's `0xFC` is SE): what the
  `GetCombatTarget` condition (22318) and Papyrus `GetCombatTarget` (54680) read. Written by the combat controller
  only; a copy's is written with its owner's (`SkyrimBridge::SetCopyAiFields`).

### The package and the other AI state fields (2026-10-22)

- **Package.** Conditions (`GetIsCurrentPackage` 21529) and Papyrus read `Actor::GetCurrentPackage` (37399):
  `AIProcess::currentPackage.package` (`+0x18 + 0x8`), or, for the engine-made procedure types 0x14-0x26 but 0x1A
  and 0x1E (`TESPackage` 29581: alarm, flee, dialogue, do-nothing...), the form package under it (`ExtraPackage`).
  Open Animation Replacer reads the field inline. The copy holds the owner's form package in that slot,
  put by `ActorPackage::InitFromPackage` (38959: ends the old package's data and refcount, makes the new one's data,
  sets the target; no package begin/end events) without a target, and by `HookCheckForNewPackage` when the engine asks
  for a new one. Nothing runs it: package procedures run only from `AIProcess::UpdateHighProcessAI` (39637, whose only
  caller is `UpdatePackageAndProcess` 39108, whose only caller is `UpdateAI` 37347, cut for copies). For a day it was
  answered instead (do-nothing in the slot, 37399 hooked for conditions, Papyrus and plugins) after a lock-order report
  blamed on a copy running `BGSProcedureSleep`; that report's stack runs through `HookUpdateAI`'s pass-through
  (`RealUpdateAI`), so the actor was not a copy: an NPC of the guest's own sleeping just after the guest left the host
  to load for a late join, while the main thread loads cells (seen with either version, `late-join-gets-ring-pose`).
  The engine's other readers of the slot (Wait, StopWaiting, the sneak decision 40492's always-sneak flag,
  0x140701B60, 0x14076D520) now see the owner's package; the sneak toggle a copy would make is refused
  (`HookPerformAction`).
- **Sit/sleep state** (`actorState1` bits `0x3C000`, `GetSitting` 21441, `GetSleeping` 21440): a copy already gets
  its owner's from the replayed furniture actions (`furniture-*`) and the graph state at spawn; nothing written.
- **Occupied furniture** (`MiddleHighProcessData +0x208`, `IsCurrentFurnitureRef` 21442 through 0x1407127C0) and
  **follow target** (`AIProcess +0x110`): not sent (no vanilla dialogue line reads them; the furniture field stays
  set after the actor leaves the furniture, e.g. Vilkas's bed in QASmoke); the status shows them.
- **When it changes.** `TESPackageEvent` (actor `+0`, package form id `+8`, type `+0xC`: 0 start, 1 end, 2 change, as
  the VM's sink 53984 reads it; sent by 39434-39436 through `BSTEventSource<TESPackageEvent>::SendEvent` 39769, from
  `Actor::EvaluatePackage`, the procedures and scene actions, some on the AI's job threads), `TESCombatEvent`
  (CommonLibSSE-NG: actor, target, new state; not sent for a target switch alone), `TESDeathEvent` (a death clears
  the package with no package event: `death-stage-runs-once` caught a copy keeping Valdr's), `TESSceneEvent`.
- **Scene.** `IsInScene` (21682) and Papyrus ask `Actor::GetCurrentScene` (37247, vfunc 0x4A: `boolBits & 8`, then
  the reference's scene data): hooked for copies (the owner's scene).
  Native scene stop24140 emits TESSceneEvent before actor cleanup3A2570 clears the scene through vfunc0x4B.
  The owner's event read can therefore retain the ending scene. AiStateService refreshes owned actors with
  active last-sent scenes on UpdateEvent and publishes their actual final state, including replacements;
  inactive scenes need no polling. `[scene-clear]` and `[scene-refresh]` cover the ordering and lifecycle.
  [Original Belethor evidence and native barter controls](../../reports/2026-10-31-v1-three-risks.md#belethor-scene-and-reopening).
- **Headtracking** (built 2026-10-31). What an actor looks at is six slots in `HighProcessData` (`headTrackTarget[6]`
  `+0x140`, handles; `headTracked[6]` `+0x158`; CommonLibSSE-NG `HEAD_TRACK_TYPE`: default, action, script, combat,
  dialogue, procedure), set by the AI, packages, dialogue and scripts through `HighProcessData::SetHeadtrackTarget`
  (39783; it also updates the cached look position, `sub_14070BEC0`) and `ClearHeadtrackTarget` (39756), with no
  event. `Actor::ProcessTracking` (38009, vfunc `0x122`: Actor, Character and PlayerCharacter share it; CommonLib
  `(float delta, NiAVObject*)`, the body reads only `rcx` and `xmm1`) runs every frame from `Actor::UpdateAnimation`
  (call at `0x14066AC22`, gated only by distance), copies included. It takes the highest set slot
  (`AIProcess::GetHeadtrackTarget` 39484; in a conversation it alternates the script and default slots), the
  target's look position (vfunc `0x2D8`), and `AIProcess::SetHeadtrackTarget` (39887) writes it to the graph
  (`SetGraphVariableNiPoint3`, the `TargetLocation` the look-at modifiers read) and sets `MiddleHigh +0x326`. A
  copy's slots stayed empty (no AI), so its head never turned: `dialogue-copy-looks-at-speaker` (2026-10-31) showed
  the host's Lydia's dialogue slot on the talking player and her copy's none, and the graph sample in
  `animation-replay-recovery.md` differing only in `TargetLocation`. Now `AiStateEvents` hooks ProcessTracking:
  an owner's slots changed since its last call raise `AiStateChangeEvent` (`ActorAiState::HeadTrack`, each slot by
  session id or form), and a copy's owner's slots (`ActorExtension::HeadTrack`) are put in before the engine reads
  them, so the copy's own engine turns its head. Compared as `ai_state.head_track`; GameTests `[headtrack]`.
  An owner beyond the distance gate is not processed, so its slot changes go out once it is close again.
- **Talking.** `IsTalking` (37266, read by the condition 21519 and the dialogue menu 35283/35290/35294): `boolBits &
  0x180` and the voice timer, which a copy's played voice line sets. Compared (`ai_state.talking`), nothing written.
- **Alarmed** (`GetAlarmed` 21452: the running package's type is 21, an engine-made alarm package): not carried; a
  copy answers no.
- **Every event sent to a graph:** `IAnimationGraphManagerHolder::NotifyAnimationGraph` (32901), called only
  through vtables (25 holder vtables, one implementation), hands it to `BSAnimationGraphManager` 63362. Actions,
  Papyrus `SendAnimationEvent` and SKSE plugins (`NotifyAnimationGraph`) all pass there; a graph's own
  transitions and clip annotations do not.

How mods read and drive AI state, from their source (2026-10-20): Open Animation Replacer's conditions read
`IsInCombat`, `GetCombatState`, `GetCurrentPackage` (inline, `AIProcess` running package),
`currentCombatTarget`, `kSearchingInCombat`, `high->greetingPlayer`, `GetCurrentScene` and `IsTalking`
(37266) on the actor whose clip activates, on every game. SCAR hooks the combat behaviour tree
(`CombatBehaviorAttackBase` 48147, the controller's behaviour threads); PapyrusUtil's package overrides act
at package evaluation; Valhalla Combat and DeadlyCombat react in the hit path and drive the graph with
`SetGraphVariable*` and `NotifyAnimationGraph`. SkyMP runs an NPC's AI on its hosting player and puppets it
elsewhere (`keepOffsetFromActor`), with no AI state synced.

Placed wildlife's base picks (2026-10-23): the Rabbit/Fox swap keeps the correct reference/session links;
`OnCharacterSpawn` used to retain the guest's local roll. The existing spawn `BaseId` now carries the host's
chosen NPC base for placed actors too. The guest applies it on spawn and refresh, keeping the reference.
`SetBaseObject` dispatches virtual 0x84 (`Actor::SetObjectReference`, 37466), whose actor override also updates
race, and reloads loaded 3D. CommonLibSSE-NG's Actor.h and 1.6.1170 disassembly at `0x140673B40` agree.
GameTests reproduces both swapped names/races, with absent 3D and reversed refresh order. The 3D reload calls the
reference's own Disable (19801) before Enable: `Actor::Disable` (37267) only queues task 80 (`0x1406569E0`), which runs
19801 later, after the Enable, and left every swapped actor disabled. Skyrim: `wildlife-takes-hosts-pick`.

### Still open

- `Actor::Update` on copies: the trespass rule (only the real player trespasses, so a remote player is
  never caught on the other game; decided answer: a shared party bounty, copy-contract.md). A copy
  that surfaces before its owner's gasp arrives could gasp on its own (above): not seen, the copy lags
  its owner.
- AI patches inherited from upstream, from before copies ran no AI: `HookIsFleeing` (forces "not
  fleeing" for players; its comment asks why the flag gets set on remote players at all) and
  `HookProcessResponse` (a remote player's copy speaks no AI dialogue response), and the detection and
  anger hooks (`HookUpdateDetectionState`, `HookIsHostileToActor`). Re-check each during the
  `Actor::Update`, combat and detection audits: delete it if the cut already covers it.

- Once, after 376 hot reloads, the client crashed in the harness's `strip` (`EquipManager::UnequipAll`
  → `InventoryChanges::VisitWornItems` reading a garbage list node on a copy). Not reproduced on fresh
  games (5 of 5).
- First-person owner, jump-land, magic equip and cast within ~250 ms: the copy's third-person graph is
  still landing or equipping when the cast arrives (3 refusals in the last measurement). The owner's
  own third-person graph keeps up (20/20 casts accepted in its state), so the difference is in the
  copy's graph timing around `JumpLandEnd`, not in a missing event. Not yet explained.
- More synced Master_Behavior variables are graph outputs of the same kind: `BSIsActiveModifier`
  outputs and state machines' `isActive`, e.g. `IsCastingRight/Left`, `bMRh_Ready`, `IsEquipping`,
  `IsAttacking`, `bInJumpState`. List them with `behavior_dump.py writers`. They were left synced
  because no trace showed harm yet, and some names are also used by the engine (in the exe).
  `currentDefaultState` is also synced by the troll, draugr, werewolf, steam and falmer descriptors.
- With `HookUpdateAI` on the path, the lock checker reported `+0x768b30 > +0x3d1507 vs +0x3d21c8 >
  +0x767bdb` as `[ours]`: an NPC's package travel under `UpdateLowActorsAI` against a Character being
  constructed during a cell load, our frames only the pass-throughs `HookUpdateAI` and
  `HookCharacterConstructor`. Both stacks read (2026-10-12); in `scripts/lock_order_known.txt`.
- The copy's `ActorMagicCaster` still writes `bWantCast*` when `NotifySpellCast`/`NotifyInterruptCast`
  arrive, on the network's time base, not the replay's.
- NPC copies refuse `turnStop` now and then (wolf, Lydia). Not traced.
- `TESObjectREFR::GetParentCell` (our virtual) returned null for the player in an exterior right after
  a `coc` while the `parentCell` field was set: the vtable slot may be wrong. Arrival spacing reads the
  field. The harness's `GetCellObjectIds` still calls the virtual.

## Holding an NPC still (test harness, 2026-10-20)

A scenario that hits or talks to one of the game's own NPCs needs it in a known spot, standing still, whatever
its schedule was doing. Harness commands `move_actor` and `freeze` (`TestHarness/EngineCalls`: `MoveActorToPlayer`,
`MoveActorHome`, `FreezeActor`); `coop_test.py` `stage_host_npc` / `release_host_npc`.

| Step | Engine function | Why |
|---|---|---|
| Out of furniture | `Actor::StopInteractingQuick(true)` (38697) | A bed or chair's entry and exit animations move the actor whatever its AI does (Vilkas walking into bed with the AI off). `true`: no exit animation, sit/sleep state 0, furniture cleared. `MoveTo_Impl` calls it for actors too |
| Move, loaded or not | `TESObjectREFR::MoveTo_Impl` (56626) into the player's cell | For actors it ends in 37313 (our `HookForceState`, a pass-through) |
| AI off | `kProcessMe` cleared, controller velocity zeroed: what `Actor::EnableAI(false)` (37504) does | No packages, no combat reactions (blocking) |
| No path | `Actor::StopMovement` (37818) | The mover walks a path on otherwise (po3 Tweaks' ToggleGlobalAI fix) |
| Held | `Actor::SetRestrained(true)` (37488): life state 6 and `StopMoving` | Undone on thawing only if freezing set it |
| Not pushed, passed through | controller flags `kNotPushable`, `kNoCharacterCollisions` set, `kRecordHits`, `kHitDamage` cleared (`bhkCharacterController::flags`, `+0x218`) | As PapyrusExtender's `FreezeActor`; restored on thawing |
| Back home | `TESObjectREFR::MoveToMyEditorLocation` (19516), what Papyrus `MoveToMyEditorLocation` queues | |

Order matters. An unloaded actor's `MoveTo` completes in its own update, so freezing it before it has arrived
keeps it where it was. And its own package can bring it into the cell first, elsewhere: Vilkas, sent for the
player by the Companions' quest, arrives at the cell's door, after our move. `stage_host_npc` therefore moves
it, waits for it in the cell wherever it lands, freezes it, moves it again within the cell, and freezes once
more for the controller it has by then.

Seen while building it: a frozen actor still takes a melee hit's knockback (about 8 units), and an NPC heals
between hits, so a scenario steps up before each swing and judges each hit against full health. A host NPC
that leaves the cell takes the client's copy of it (the client's own persistent reference) along, still a copy
(`docs/design/copy-contract.md`, "When the owner's actor goes away"). `release_host_npc` sends the host's home
while still frozen: thawed and unloaded, its AI walks it on in jumps of ~300 units, which the copy does not follow.
