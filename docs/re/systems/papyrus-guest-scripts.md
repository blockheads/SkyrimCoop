# Papyrus: guests' characters are the player (host)

Engine side of `docs/design/party-model.md`, "A guest's actions on the host", rules 1 and 2
(`Code/client/Games/Skyrim/Papyrus/GuestScripts.cpp`). Found in the 2026-10-19 spikes (`docs/research/player-events.md`).

## Rule 1: comparisons

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 105220 | `Internal::CodeTasklet` value comparison (CMP_EQ and the others call it) | `HookCompare` | `this` is the running tasklet (`+0x10` its stack). Objects compare by handle: two `Object`s with one handle are equal. On the host, the player (0x14) against a remote player's copy returns 0 (equal), whatever names the player: `Game.GetPlayer()`, a `PlayerREF` property, a variable that stored it. |

Upstream had this hook (`b87e5820`, `HookCompareVariables`) and dropped it for beast form: the check passed
for the copy, then `Game.GetPlayer()` handed the effect to the host's player. That path no longer exists:
the target's owner applies spell effects (`docs/design/copy-contract.md`), so the change effect's script runs
on the guest's game only and the host never sees `Target == Game.GetPlayer()` for a copy
(`client-beast-form-stays-client`, 2026-10-20; below).

## Rule 2: Game.GetPlayer() is the guest whose action started the script

A script stack runs for a guest when:

- its call was queued with that guest's character as an argument (the engine's event for the copy's action:
  `OnActivate`, `OnTriggerEnter`, `OnHit`...), or
- it was queued while a native ran for that guest (an `Activate` raising `OnActivate`, a `SetStage`
  running a fragment), or
- it answers a registration made for that guest (`RegisterForSingleUpdate` → `OnUpdate`; the repeating
  `RegisterForUpdate` until `UnregisterForUpdate`), or
- it is a dialogue line's fragment after that guest's pick (2026-10-22): `TESTopicInfo::Begin` (25616) sends
  the topic info event for the speaker at once, at the line's start and its end, and the VM's sink (53997)
  queues the fragment on that thread; the dialogue hook wraps it in `ScopedCause` for the character
  `HostConversation` names (`IDialogueBridge::Filters::LineFor`).

**This game's own player, and the story** (2026-10-23, for `PartyNatives`: `docs/design/cutscenes.md`, "Whose
moment"). In a session the same tags also name this game's player (0x14) on the host, as they always did on a guest:

- a call the engine queues (no native running: `t_nativeDepth`) with the player as an argument;
- an event `WorldEvents::IsOwnEvent` calls the player's own (SKSE's input and menu events, an event on the player,
  a timer the player's scripts set), when no native runs and no cause is set;
- the host's own picked line (`HostConversation::LineFor` returns 0x14).

A 0x14 tag changes nothing for rule 2 (`GuestCharacter(0x14)` is null). `MagicTarget::IsWorldCast` treats it as no
guest. A `SetStage` (`NativeReach::Call::kWorld`) running in a stack with a player's cause marks what it queues as
the **story** (`Cause::Story`: its fragment), and the mark follows that stack's natives, the calls they queue and
the timers they set.

| ID | Engine function | Our hook | What it does here |
|---|---|---|---|
| 104943 | `Internal::RawFuncCallQuery` constructor `(callType, objectType, name, self, args)` | `HookCallQueued` | Decides the guest a queued call runs for (above); arguments already built, read without engine calls. |
| 104848 | `VirtualMachine`: queue a call message `(stack, query)` | `HookStackCreated` | The stack is allocated with the query: the call's guest becomes the stack's (`Stack::stackId`, `+0x80`). Seven dispatch paths call it. |
| 104480 | `Stack::Dtor` | `HookStackEnded` | Forgets the stack. |
| 104651 | `NativeFunctionBase::Call(BSTSmartPointer<Stack>&, logger, vm, bool)` | `HookNativeCalled` | Every native: while it runs for a guest, a thread-local carries the guest to what it queues. Registers and unregisters for updates. |
| 55618 | `NativeFunction0<StaticFunctionTag, Actor*>::MarshallAndDispatch` | `HookGetPlayer` | `Game.GetPlayer` (55469, bound in 55503; other no-argument natives returning an Actor share it, filtered by name, case-insensitively: below): in a stack run for a guest, the result is repacked (55510, then `Variable` assignment 104297) as that guest's character. |
| 53954, 55744 | `SkyrimVM` queuing a delayed functor; `CastFunctor::operator()` | `HookFunctorQueued`, `HookCastRun` | `Spell.Cast` casts later from a functor, maybe after the script ended: the functor is tagged with the native's guest when queued, and the cast runs for it (`docs/re/systems/guest-world-events.md`). |

- **A player in the frame** (2026-10-22): a script that stored the player (a `PlayerRef` property, a variable set
  at `OnInit` / `OnLoad`, an alias filled with the player) never calls `Game.GetPlayer()`. In a stack run for a
  guest, `HookNativeCalled` puts that guest's character into the native's frame wherever the player (0x14) is, its
  `self` (`StackFrame::self`, +0x28) and its arguments (`GetStackFrameVariable`, 104484), for the call, and gives
  the frame its own values back after it (`PlayerInFrame`). The call reads them at once (104651 copies `self` at its
  start and hands the frame to `MarshallAndDispatch`; a latent native keeps what it read), so
  `PlayerRef.RemoveItem` acts on the guest's character, as `Game.GetPlayer().RemoveItem` does, and so does a
  stored player passed as an argument (`dragonActorSCRIPT`'s `getDistance(player)`). Scenario
  `relief-takes-client-relics`; control (the swap off): it fails at "[client] the player gave up the axe".
- **Natives on the player** (2026-10-22): global natives that act on this game's player without naming them
  (`Game.AdvanceSkill`, `QueryStat`, `ShowTrainingMenu`, `FastTravel`, `Debug.Notification`...), read from their
  code, not listed (`NativeReach`, below "Natives on the player: read from the code"). In a stack run for a guest,
  `HookNativeCalled` does not run them: it reads their arguments from the native's frame (numbers, bools,
  strings, forms, None), clears the stack's return value (`Stack::returnValue`, +0x70; they return None) and returns
  `kCompleted`, as the original does after a call (read in IDA: 104651 sets +0x70 and returns 0). The host's game
  sends them to that guest's game (`PlayerNativeEvent` → `PlayerNativeRequest` → `NotifyPlayerNative`, only from
  the host's game, `HostPlayerNatives`), which queues the same call on its VM (`IVirtualMachine::DispatchStaticCall`,
  slot 0x26, read in IDA: class and function looked up, the arguments filled at once by `IFunctionArguments`' slot 1,
  then the stack queued through 104848), so its own player, perks and mods take it. An argument the session cannot
  carry (an array, a temporary reference that is not an actor: "Values carried" below) leaves the native on the
  host, with a warning.
- **A native returning a value** (`QueryStat`, `CalculateFavorCost`, `GetPlayersLastRiddenHorse`) waits for the
  guest's game, as a latent native does: `PlayerNatives::SendToGuest` puts the stack in the VM's latent wait (104851, what
  104651 calls for a native flagged latent; it logs "requested to wait on a latent call") and returns `kInProgress`,
  which the VM's caller (104853) handles without looking at the function's latent flag. The guest's game dispatches
  the call with an `IStackCallbackFunctor` (CommonLibSSE-NG; the VM tells it the stack's result), sends the value back
  (`PlayerNativeResultRequest`, the stack id as `ReplyId`; `HostPlayerNatives` takes it only from the game it asked
  and passes it to the host's game as `NotifyPlayerNativeResult`), and the host's game resumes the stack with it
  (`IVirtualMachine::ReturnFromLatent`, slot 0x2B, 104807: sets `Stack::returnValue` and runs it on). A guest that
  leaves first, or a call its game cannot run, answers None: for a number, a bool or a string the script gets its
  return type's default (`GetReturnType`, slot 4, 104635, returns a `TypeInfo` through a hidden pointer). The round trip is a few frames
  (about 50 ms in `dialogue-picker-gets-speech-experience`). Not handled: a save taken while a stack waits keeps the
  wait in the save with nothing to answer it after a load (vanilla latent natives save their own functors).
- Guests' characters are known by form id (`SkyrimBridge::SetActorCopy` / `SetActorPlayer` / `DeleteActor`
  keep the list through `GuestScripts::SetGuest`, filled only while hosting), so the constructor never
  resolves a form: it can run with engine locks held (spike 1: building arguments there added lock orders
  the engine never takes). The character is confirmed (`TESForm::GetById`, `IsOtherPlayer`) only where it
  is used, in script threads.
- Native and event names are compared case-insensitively (`SameName`, `engine-names.md`): a name reads
  back from the engine's string pool in whichever spelling was pooled first.
- Measured with the lock-order checker after `plate-counts-client` and the combat, door, lock and travel
  scenarios: no report with these hooks on the stack.

## Natives on the player: read from the code

A global native (`StaticFunctionTag`, no self) has no target: when its code reaches the player or the screen,
that can only be this game's. So in a stack run for a guest, a global native whose C++ function, or a callee down
to three calls (tail jumps included), addresses one of this game's per-player objects runs on that guest's game
(`Papyrus/NativeReach`, `Papyrus/PlayerNatives`). The objects, by Address Library id: the player
(`g_thePlayer` 403521, `gPlayerCharacter3` 401069), its misc stats (`MiscStatManager` 400662), its screen (`UI`
400327, `UIMessageQueue` 400445, `PlayerControls` 400864, `ControlMap` 400863, `MenuControls` 401263,
`PlayerCamera` 400802, `ImageSpaceManager` 414660, `BSInputDeviceManager` 402776, `ScreenSplatter` 400672, the
gamepad's rumble queues 410222 and 410225). `Game.GetPlayer` is rule 2's own (`HookGetPlayer`, answered on the
host). Natives with a self are not read: they act on what they are given (below, "Naming their target").

- **Read at run time**, once per native, on its first call in a guest's stack: its function is `+0x50` of the
  engine's `NativeFunctionN` (and SKSE's; its `MarshallAndDispatch`, vfunc 0x16, does `mov rax, [r14+50h]; call
  rax`), or the callee of a CommonLibSSE native's `std::function` there (MSVC: the `_Func_impl` in place, vtable
  then the pointer, `_Impl` at +0x38 pointing at it). Zydis decodes each function over its unwind entry
  (`RtlLookupFunctionEntry`; a leaf without one until it returns, at most 0x400 bytes); every RIP-relative operand
  is checked against the objects' addresses, and `call`/`jmp` targets that start a function are followed, in the
  game and the native's own module only. In a plugin, an object or function is also reached through the data cell
  holding its address (a `REL::Relocation`).
- **Checked against IDA:** `scripts/re/papyrus_natives.py` reads the same rule over the database (663 natives, 143
  global; 65 reach one, depth 2 and 3 alike, depth 5-6 adds unrelated engine code: Havok constraints,
  `DumpAliasData`). Its output is `docs/re/papyrus_natives.tsv`; scenario `natives-reach-matches-ida` asks the
  plugin (harness `global_natives`) for every global native IDA lists and fails on any read otherwise.
- **The 65, each right on the guest's game** (read 2026-10-22): its stats, skills, perks and achievements
  (`AdvanceSkill`, `IncrementSkill(By)`, `IncrementStat`, `QueryStat`, `AddPerkPoints`, `AddAchievement`,
  `CalculateFavorCost`, `GetPlayersLastRiddenHorse`); its menus (`ShowTrainingMenu`, the race menus, the title
  sequence, `Debug.MessageBox`, `Debug.Notification`, `ToggleMenus`, `TakeScreenshot`); its screen (fades, camera,
  controls and their queries, `SetPlayerAIDriven`, `ShakeCamera`, `TriggerScreenBlood`, sun gazing, the HUD's cart mode, `UsingGamepad`, `ShakeController`); its body and place (`FastTravel`, `CenterOnCell(AndWait)`, `PlayerMoveToAndWait`,
  `SetBeastForm`, `SendWereWolfTransformation`, `ClearPrison`, `SetPlayerReportCrime`, `TeachWord`, `UnlockWord`);
  reads around it (`FindClosestActor`, `FindRandomActor`, which skip the player; `GetRealHoursPassed`,
  `GetPlayerGrabbedRef`); `QuitToMainMenu`, `PrecacheCharGen`; the player's fast travel flag, jail time and
  character creation flags (`EnableFastTravel`, `ServeTime`, `SetInChargen`: leaves IDA had not made functions,
  found by the plugin's reading first). Not tested: a carriage ride (`CarriageSystemScript` travels from the stack
  of the player sitting down, not the picked line's).
- **The screen's calls come first:** a native whose own code takes the controls or changes the screen (fades,
  camera, shakes, music) runs on every game, not on the guest's alone, though it reaches the player too (`PartyNatives`, `docs/design/cutscenes.md`). On a guest, its own script's call setting a quest's stage
  goes to the host (`PlayerNatives::SendToHost`).
- **Misses stay on the host**, as before: a native reaching the player through a virtual call or a queued task, or
  an object not in the set. `PlayBink` reads the movie player (`gBGSMoviePlayer` 412214), left out on purpose: a
  video is a cutscene, which the whole party sees (`party-model.md`, Cutscenes), so it waits for the cutscene work to
  run on every game. The fix is the object, never a native's name. A CommonLibSSE plugin's relocation is filled on its first use: a
  native read before it stays on the host.
- **Values carried:** numbers, bools, strings, None, and forms: an actor in the session by its session id, any other
  form by its `GameId` (`ScriptValue`, `ScriptForm`), so `FastTravel`'s marker, `TeachWord`'s word or
  `GetPlayerGrabbedRef`'s reference go as well. A form is read from a form handle only (bits 32-47 `0xFFFF`; an
  alias's or an active effect's names its quest or actor), and written back as a script object of its own type
  (`Variable::SetForm`): the engine packs per type (55510 is an actor's, type 62: the class by type id, the handle,
  the bound object or a new one bound, `ObjectBindPolicy::BindObject` 104184), so ours does the same with the form's
  type (VM slots 0x0A, 0x15, 0x1C, 0x2D, 0x2F, read in IDA; CommonLibSSE-NG `PackHandle`). Not carried, so the native
  stays on the host with a warning: arrays, and a temporary reference that is not an actor (no other game knows
  it). Scenario `words-reach-party`: the client presses Dragonborn's QA shout button in QASmoke
  (`DLC2QALearnShouts`), the host's script teaches and unlocks its twelve words for the client, the client's game
  learns them, and (feature 11, `docs/design/dragonborn.md`) its player's words are the party's: the host's learns
  them too.
- `ClearPrison` and `SetPlayerReportCrime` change the guest's own state; the party's bounty (the `Faction` crime
  gold natives, with a self) stays the host's. Natives with another actor as their self that open this player's
  menus on it or read this player against it (`Actor.ShowGiftMenu`, `ShowBarterMenu`, `OpenInventory`,
  `GetBribeAmount`, `WillIntimidateSucceed`) are methods, not read: barter and gifts on the picker's game are
  `shared-dialogue.md`'s (Open, item 1).

**Naming their target:** the natives with an actor or a reference as their self or an argument (`Actor.AddSpell`,
`ObjectReference.MoveTo`, `Quest.SetObjectiveDisplayed`...) read the player to compare it with that target. They
act on whoever they are given; a stored player given to them in a guest's stack is that guest's character (above,
"A player in the frame").

## Tests

Unpatched vanilla scripts, the client acting on its own game; checked on the host.

| Scenario | Script | Rule | What the host shows |
|---|---|---|---|
| `plate-counts-client` | Kolbjorn Barrow pressure plate (`DLC2dunKolbjornPressureTileScript`, a player stored at `OnInit`) | 1: a stored player | the plate goes `inTrigger` under the client's character and back to `Waiting` behind it (harness `script_state`: `FindBoundObject`, VM vfunc 0x1C, then `Object::currentState`, `+0x10`) |
| `sword-box-asks-client` | Rebel's Cairn sword in the stone: the box (`dunRebelsCairnActivator`, `obj == Game.GetPlayer()`) activates the manager (`dunRebelsCairnSwordInStoneManager`), whose new stack asks `Game.GetPlayer()` for the sword, takes it, opens the door; the sword in the stone gives it back | 1, and 2 through a call queued by `Activate` | the secret door opens though the host's player has no sword, closes again, and the host's player never gets the sword (harness `item_count`) |
| `ring-turns-for-client` | A claw door's ring (`HallofStoriesDiskScript`, `actorRef == game.GetPlayer()`, state `positionN`), the E3 demo's Bleak Falls Barrow | 1 | the host's ring turns three times round as the client turns it; the client's own ring script stays (step 5) (rings only finish turning in view: the players stand before the door) |
| `keyhole-asks-client` | The same door's keyhole (`HallofStoriesKeyholeScript`: `actorRef == game.GetPlayer()`, then `game.GetPlayer().GetItemCount(claw)`), a ring wrong | 1, and 2 in the handler | the keyhole takes the try (`busy`, then back to `mainState`) though the host's player holds no claw; the door stays shut |
| `shrine-blesses-client` | Solitude temple shrines (`TempleBlessingScript`: casts on the activator, messages when it is the player) | 1, and 2 through `Spell.Cast` | each player gets only their own shrine's blessing on every game, though each game sees the other's character pray; the client's comes from the host's script (step 5), cast by its copy and applied by its game |
| `vilkas-counts-client-hits` | The Companions' trial (`C00TrainerScript` on Vilkas's alias: hits by `Game.GetPlayer()` count, anyone else's end the trial) | 1 | three hits pass the trial (stage 150, not 110). Nothing the hits start asks for the player: stage 150's fragment only stops the fight, and `PassTheSword` (`Game.GetPlayer().AddItem`) is stage 200's, set from his dialogue (`QF_C00VilkasTrainingQuest`: 150 → `Fragment_3`, 200 → `Fragment_1`) |
| `statue-fires-at-client` | Soljund's Sinkhole boss trigger (`dunSoljundsBossSCRIPT`): `RegisterForSingleUpdate`, every seventh `OnUpdate` the statue casts Firebolt at `game.getPlayer()` | 1, and 2 through `RegisterForSingleUpdate` | the host's own hit events (harness `engine_events`, `hit`): the statue's Firebolt (`0x12FD0`) hits the client's character and never the host's player, who stands 3,500 units away |
| `dialogue-options-are-the-hosts` | Brenuin's coin (`TIF__00060022`: `Game.GetPlayer().RemoveItem(Gold, 1)`, the blessing cast on `Game.GetPlayer()`) | 2 through a picked line | the client's coin goes, the host's gold stays |
| `dialogue-pick-says-the-hosts-line` | Hulda's room (`RentRoomScript.RentRoom`: `Game.GetPlayer().RemoveItem`) | 2 through a picked line | the host's gold stays; the client, judged by the host's list, has none to give |
| `dialogue-picker-gets-speech-experience` | Ungrien's persuade (`TIF__00036A32`: `FavorDialogueScript.Persuade`: `GetAv("Speechcraft")`, `AdvanceSkill`, `IncrementStat`, `QueryStat`, `AddAchievement`) | 2, natives on the player, one returning a value | the client's character on the host carries the client's Speech; the client's Speech gains it, the host's not; `QueryStat` answers with the client's stats, so the achievement runs on the client's game; the host's own persuade stays the host's |
| `relief-takes-client-relics` | Volunruud's relief door (`dunVolunruudDoorSCRIPT`: `player = game.getPlayer()` at `onLoad`, then `player.getItemCount(relic)`, `player.RemoveItem(relic)`) | 2, a player in the frame | the client's axe goes into the relief on both games though the host's player has none; the host's box says the sword is missing (stage 10 stays: stages only go forward) |
| `dialogue-picker-trains` | Amren's training (`Game.ShowTrainingMenu(akSpeaker)`) | 2, natives on the player | the training menu opens on the client's game over its dialogue menu (as on the host's), never on the host's |
| `client-beast-form-stays-client` | Beast Form (`WerewolfChangeEffectScript`: `Target == Game.GetPlayer()` starts `PlayerWerewolfQuest`, whose `SetRace` changes `Game.GetPlayer()`), upstream's reason for dropping rule 1 | none: the effect is the guest's | the guest changes and changes back, the host shows its copy do the same (respawned for each race), the host's player keeps its race and the host's `PlayerWerewolfQuest` never runs |

The ring, keyhole, shrine and trial scripts are the ones upstream shipped patched copies of (`SkyrimTogetherUtils.IsPlayer`,
`IsRemotePlayer`, the trial's player checks commented out); the patches and the natives are gone, and these
scenarios run the vanilla scripts. Vilkas is brought to the home cell's stage and held there by the harness
(`docs/re/systems/actor-ai-and-copies.md`, "Holding an NPC still (test harness)").

Each scenario has to leave the world as it found it: these scripts run once per state, so a run that stops
halfway leaves the next one nothing to trigger. The sword scenario takes the sword back; the Soljund trigger
re-arms when its cell detaches (it stays in `polling`). Control: with `SKYRIMCOOP_DISABLE_HOOKS=GuestScripts` (2026-10-20)
all three fail at rule 1's step (the plate stays up, the door stays shut, the trigger never polls). Rule 2
has no control of its own (the hooks go out together); its scenarios are built so that only the client's
character can pass: the host's player has no sword, and the client's statue is disabled.

Found while writing them:

- Both games ran the client's world scripts until world-state sync step 5 (2026-10-21): the client's own box
  took a sword on its game, and the host's `AddItem` to the copy reached the client's player too, so the
  client ended with two swords where one went back. Now the client's box runs nothing: one sword goes and
  comes back once, and the scenario checks it.
- A NiController door (the secret door) only plays its `Open` / `Close` sequence while a camera sees it, and
  `GetOpenState` reports `opening` (2) until then (`BGSOpenCloseForm::GetOpenState`, 14288: the sequence's
  state). A scenario that waits on such a door stands the players where they see it. Harness `door_open`
  (`BGSOpenCloseForm::SetOpenState`, 14287) puts a door back in one game.
- The copy took Red Eagle's sword into its hand when it got one on the host, while its player held another
  sword: the next baseline check found the copy's hands differing from its player's (not followed up).

### Rule 2 never ran (found and fixed 2026-10-20)

`sword-box-asks-client` ("[host] secret door C149D opens") and `keyhole-asks-client` ("[host] the keyhole takes
the claw") failed on every fresh duo. Logged from the hooks during the keyhole run:

```
OnActivate queued on the keyhole, args [the copy]  → call tagged for the copy (rule 2's tagging works)
stack created for that call                        → stack tagged for the copy
GetPlayer runs in that stack                       → name reads 'getplayer'; strcmp(name, "GetPlayer") != 0
                                                   → result not repacked: the host's player
actorRef == game.GetPlayer()                       → true (rule 1)
game.GetPlayer().GetItemCount(claw)                → 0: the host's player has no claw → message, no "busy"
```

The sword box failed the same way (`ActivateBox` asks `Game.GetPlayer()` for the sword). The fix compares
names case-insensitively (`SameName`), also for the timer natives and their events, which matched only
because they happened to be pooled in the spelling we wrote. Why the pool holds `getplayer` is not known
(something pooled that spelling first); it was the same on every fresh duo since before the beast form work.

`vilkas-counts-client-hits` and `statue-fires-at-client` passed with rule 2 dead too:

- The trial's sword is given at stage 200, from dialogue, which the scenario never reaches. The scenario
  now claims rule 1 only. Rule 2 through `SetStage` has no scenario.
- The statue's fire on the client's character was the client's own: the client's game runs the trigger
  too (world-state sync step 5) and its statue fired at its own player though the scenario had disabled it
  (`6AE8C.disable` on the client; why it still fires is not known). The copy on the host mirrors that hit.
  With rule 2 dead the host's statue aims at the host's player: its Firebolt leaves at a heading of 2.37
  (toward the host's player) instead of 1.52 (toward the client's character), read from
  `Projectile::LaunchData` (`angleZ` `+0x48`, CommonLibSSE-NG). The scenario now checks the host's own hits
  (harness `engine_events`, `hit`: `TESHitEvent`, target, cause, source `+0x10`, projectile `+0x14`) and fails
  with rule 2 off (control, 2026-10-20).

### Beast form (2026-10-20)

The chain on the guest's game, as `client-beast-form-stays-client` waits on it (harness `engine_events`):

```
Shout key (a power fires on release) → spell_cast 92C48 Beast Form
  → magic_effect_apply 92C45 → WerewolfChangeEffectScript.OnEffectStart
       Target == Game.GetPlayer() → PlayerWerewolfQuest.Start()   (2BA16; stage 0 PrepShift: controls, UI)
       VFXSpell.Cast(Target)      → magic_effect_apply F8209 → WerewolfTransformVisual.OnEffectStart
            PlayIdle IdleWerewolfTransformation (2A59D), then on its "SetRace" graph event
            or after Utility.Wait(10): SetStage(1) → InitialShift → SetRace(WerewolfBeastRace CDD84)
  setstage 100 → ShiftBack: SetRace(C00's PlayerOriginalRace), Wait(5), Shutdown → SetBeastForm(false)
```

- The first change of a Companions member is the rampage's: `CompanionsHousekeepingScript.PlayerIsWerewolfVirgin`
  (C00, 0x4B2D9) makes the effect only clear the flag. It is false unless the Companions setup ran; the
  scenario sets it with harness `script_var` (console `setpqv` is not needed, and `player.cast` casts nothing
  in this setup: no engine event at all).
- `WerewolfTransformVisual` waits out its 10 seconds whatever happens: changed back before that, it calls
  `SetStage(1)`, which starts the quest again and changes the player again. The scenario changes back only
  once no stack runs that script (harness `script_stacks`).
- The guest respawns its copies on each race (`CharacterService::OnPlayerLooksChange`, on every
  `TESSwitchRaceCompleteEvent` of the player's: entering by `InitialShift`'s `SetRace`, leaving by `ShiftBack`'s). The respawn carries the owner's state now
  (`RequestRespawn::HasState`: latest action, graph state, values, inventory): the host rebuilt the new copy
  from the record of its last cell entry before (its magicka from before the harness's `setav`), and the
  host's copy, read for it, is already gone when the host asks (`CharacterService::RespawnCopy`).

### Harness readouts for scripts

| Command | What it reads | Engine |
|---|---|---|
| `script_state` | a script's state on a form | `FindBoundObject` (VM vfunc 0x1C), `Object::currentState` `+0x10` |
| `script_var` | a script variable, or an auto property's `::<Name>_var`; sets an int, float or bool | `Object::type` `+0x08`, variables after the object (`+0x30`), the type's `VariableInfo` list after its user flags in `data` (`+0x30`), a parent's variables first (CommonLibSSE-NG `Object::GetVariable`) |
| `script_stacks` | every running stack (running, in a latent wait, queued) as its frames' `Script.Function` | `Internal::VirtualMachine::allRunningStacks` `+0x9320` under `runningStacksLock` `+0x9318`; `Stack::top` `+0x60`, `StackFrame::previous` `+0x08`, `owningFunction` `+0x10` |
| `engine_events` | the script events (spell cast, magic effect apply, effect start/finish, hit, quest start/stop, equip), numbered | `EventDispatcherManager` sinks (14298) |

`script_stacks` reads frames another thread may be changing; it runs where harness commands run (the game
thread, outside the VM's update), where the VM leaves its stacks alone.

Quest rewards (2026-10-27): [quest-rewards.md](../../design/quest-rewards.md). Before SendToGuest selects the
picker's game for a global native, PartyNatives::Scope routes a progression native (PartyRules::RouteOf: its
cause, story and source); a shared grant is relayed as the call and runs on every game's own player, tagged mirrored
(`dragonborn.md`, "The party's calls"). Non-rewards still use PlayerNatives. HostConversation::OnPickSent sets 0x14 before the native menu picks: a begin fragment can queue
before NotifyDialoguePicked returns, so clearing the picker there misclassifies the host's persuade as the story.
