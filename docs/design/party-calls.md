# Party calls: where a script's native runs

Every native a script calls in a session goes through one decision (`PartyRules::RouteOf`), made by one scope
(`PartyNatives::Scope`, inside `GuestScripts::HookNativeCalled`). Built 2026-10-27 (feature 12's groundwork). It
replaced four separate steps, each with its own way of classifying and its own rule:

- `PlayerNatives::SendToHost`
- `Scope`'s progression path, a name list
- `Scope`'s screen kinds
- `PlayerNatives::SendToGuest`

The rules it carries are `party-model.md`'s ("A guest's actions on the host"), `cutscenes.md`'s and `dragonborn.md`'s.

```text
a native is called (HookNativeCalled 104651)
  rule 2 first: in a stack a guest's action ran, the player in its frame is the guest's character (PlayerInFrame)
  Scope: the facts -> RouteOf -> where
    kHere       runs here only
    kHost       a guest's world change: skipped here (true), the host's game makes it for that guest (RoutedCallEvent, to the host)
    kCauseGame  on the player in a guest's stack (host): skipped here, the guest's game runs it (RoutedCallEvent, to the owner),
                one returning a value waits for its answer
    kEveryGame  runs here (unless Original is off), then on every other game (RoutedCallEvent, to the others), each on its own
                player (Mark), and on the host's own player too when it named a guest's character (ThisPlayer)
```

## The model (2026-10-28)

Two questions decide every call; the rule below is their answer per kind.

```text
HOME: where does what it touches live?
  the world (NPCs, refs, story quests, globals)          -> the host
  a player (body, items, skills, screen, its system
            quests and their globals)                     -> that player's game
SUBJECT: who is "the player" in the script?
  this game's player   its own body's scripts, a system quest's own calls, a guest's own scripts
  a guest              on the host, the guest whose action started it (rule 2)
  the party            the host's scripts with nobody's action (a timer, the story); only the host spreads

write: the world -> the host;  a player -> the subject's game (the party: every game, each its own)
read:  on the home game the real thing; on the host about a guest, its copy, a full stand-in for its player
       (items, factions, race, looks, level, perks, skills; dialogue conditions judged for it: shared-dialogue.md)
```

Where a kind bends it, the rule says so: rewards are the party's whoever's action ran them, controls are the party's
only if they took movement (read after they ran), a show is every game's, a question with no cause is asked of the
nearest player, and a story's spell or faction on a guest's character runs on the copy too (`OnCopy`: the copy's
mirror of its player is seconds behind, and the story's next condition reads it at once).

## What a native reaches: read from its code

Once per native, `NativeReach::CallOf` reads its C++ function with Zydis (`CodeReach`). It records:

- the engine functions it calls directly (tail jumps included);
- the virtual calls it makes on its self or on this game's player;
- the player's fields it writes.

Each of these is looked up in one engine table, `NativeReach::EngineSinks`:

| Reach | Engine functions (Address Library ID) |
|---|---|
| World | `TESQuest::SetStage` 25004 |
| Item (ranked above the screen: an equip's HUD message is no message of the party's) | `ActorEquipManager::EquipObject` 38894, `UnequipObject` 38901 |
| Controls | `ControlMap::ToggleControls` 68545 |
| Screen | fade 52847, image-space 18570 18573 18575 18577, `ShakeCamera` 33012, camera 50790 50796 50838, music 35145 35146, rumble 68528; the movie player read (412214); messages (rule 5): the HUD's `SendHUDMessage::ShowHUDMessage` 52933, `Debug.MessageBox`'s box 52269, `Message.Show`'s box 442726, `HelpMessageManager` 51587 |
| Show | `ApplyArtObject` 22769, `ApplyEffectShader` 19872, their stops 41396 41395, sub-graph event 63372 |
| Progress, any script's but a body's | `SetPlayerKnows` 14639 (words), `IncrementSkillFromPapyrus` 55616, `PlayerCharacter::AddPerk` 40681 / `RemovePerk` 40682 (vtable 0x7D8 / 0x7E0), `ActorValueOwner::ModActorValue` 38467 with modifier 0 (the permanent one; +0xB8, slot 0x30), the player's perk points (field 0xB09) |
| Progress, the story's | `PlayerCharacter::AddSkillExperience` 40488 (vtable 0x7B8), `Actor::SwitchRace` 37925 (`Actor.SetRace`: never on a copy; a copy takes its player's race with its looks) |
| Progress, the story's, kept on a guest's character | `Actor::AddSpell` 38716, `TaskQueueInterface::QueueRemoveSpell` 36962, `Actor::SetFactionRank` 37677, `Actor::RemoveFromFaction` 37688 |
| Body (ranked after items, before the controls) | `PlayerCharacter::SetAIDriven` 40586, `PlayIdle`'s and `PlayIdleWithTarget`'s call 55105 (to `AIProcess::SetupSpecialIdle`), `SetVehicle`'s 37903, `Actor::SetRestrained` 37488; and an object a native takes the address of: `SkyrimScript::MoveToFunctor`'s vtable 218380 (`MoveTo`, `MoveToNode` queue one, `SkyrimVM::QueuePostRenderCall` 53955, and the move runs after the frame) |

A second reading answers whether a global native acts on this game's player: its code, three calls deep, reaches a
per-player object (`NativeReach::ReachesThisPlayer`; `docs/re/systems/papyrus-guest-scripts.md`). It is read only
in a stack a guest's action ran, where the rule asks for it.

### Virtual calls

Most progression natives change the player through a virtual call, which leaves no address in the code:

- `AddPerk` is `mov rax,[r8]; jmp [rax+7D8h]`.
- `ModActorValue` is `lea rcx,[self+0B8h]; mov rax,[rcx]; xor edx,edx; call [rax+30h]`.
- `AdvanceSkill` is `mov rcx,g_thePlayer; mov rax,[rcx]; call [rax+7B8h]`.

The reader follows the registers through each native in one forward pass. It knows that r8 is the native's self (the
VM's convention: vm, stack id, self, arguments), and it knows the cells of `g_thePlayer`. A branch's target starts
from the state at the branch, met with what falls through to it. A virtual call is resolved against this game's
player's own vtables in memory, and its second argument (`rdx`) is kept when it is a constant.

**Why the second argument matters:** `DamageActorValue` and `ForceActorValue` call the same `ModActorValue`, with
modifier 2 (damage) and 0 (permanent). So `ForceActorValue` on a player is progression too, as `ModActorValue` is
(a script setting souls or a stat). It was not in the old name list.

The policy (rewards any script's, the rest the story's) is the party model's decision, so it is the one hand-kept
part: a flag on each engine function's row. A mod's native that calls these engine functions is covered with no
row of its own.

## The rule

```text
mirrored (another game's call, run here)                         -> here
in a system quest's script (system-quests.md)                     -> here (this game's own instance), but on a
                                                                     guest a World change not to the quest itself
                                                                     or its globals -> host
a guest's game, World, not a quest of its own nor a system quest's -> host (skipped here)
Progress, the host, on a player (0x14 or a guest's character), not a body's script,
  a reward, or the story's (Whose: no player's action, or a stage it set)
    on this game's player                                         -> every game
    on a guest's character                                        -> every game; this player too; the original
                                                                     only when kept on the copy (factions, spells)
Body, on a player (its self, or a global's), not a body's script:
  the story's, on the host                                        -> every game, each on its own player; on a guest's
                                                                     character: not here (its owner moves it), this
                                                                     player too
  a guest's moment on its character (the host)                    -> that guest's game
Controls, Screen, Show: the story's (a show: whoever's action)    -> every game (the player marked: the story's)
  controls: shared only if it took movement (read after it ran: ShareOf)
a native on the player (a global reaching it), or a controls or screen call (a method too: a lever's Message.Show),
  in a guest's stack                                              -> that guest's game (skipped here; one returning
                                                                     a value waits for its answer)
otherwise                                                         -> here
```

Another game's progression call run here raises `PlayerProgressEvent`, so a guest's record is sent again
(`GuestRecord`), and so does a system quest's native (its instance changed).

A call into a system quest's script function is decided where it is made, not per native: a player's own action on
that player's game, the story's on every game (`GuestScripts::HookScriptCalled` on `ScriptFunction::Call` 105466,
`PartyRules::RouteOfSystemCall`; [system-quests.md](system-quests.md)).

## A body's move, idle or AI drive (cutscenes.md step 2, built 2026-10-27)

A story's call on the player's body runs on every game's own player: MQ206's `Game.GetPlayer().MoveTo(Alias_TimeWound)`
moves each player into the Elder Scroll's vision by its own `MoveTo`, a story's `PlayIdle` on the player plays on each.
Read from the code like the rest: `MoveTo` calls no move, it takes the address of `MoveToFunctor`'s vtable to queue
one, so the reader records the objects a native takes the address of (`lea`) beside the functions it calls
(`CodeReach`, `Facts::Addressed`, `Sinks::Objects`). Not read yet: `Debug.SendAnimationEvent`, a virtual call on its
argument's graph holder (`docs/bugs.md`).

A body's call ends where it runs: another game's `MoveTo` copy waits for its own move, never its original's result
(`Scope::Ran`).

**A mirrored move leads no one.** On a guest, the story's `MoveTo` mirrored there queues its player's move like a door
does, and party travel (`PlayerCharacter.cpp`, `HookProcessQueuedTravel` 40438) would announce it as the guest leading
the party, pulling the host back after its own next move. The functor carries its stack's cause as a cast's does
(`GuestScripts`: tagged at 53955 under a mirrored cause, its call 56101 runs under it), and a move it queues is marked;
`HookProcessQueuedTravel` announces no marked move (`GuestScripts::TakeMirroredMove`, one check in party travel's code).
GameTests `cutscene_flows.cpp`: the vision and back, the host travels twice, not four times.

**An engine scene casting the player** (MQ105's Greybeards: packages on the player alias, no natives): the host's
player's AI state carries its package and scene (`AiStateService`); on a guest, while another player's character is
in a scene, this game's own player runs that package AI-driven (`GuestScene`: `PlayerCharacter::SetAIDriven` 40586,
`ActorPackage::InitFromPackage` 38959), until the scene lets the host's player go. A package whose furniture the copy
already holds is left to the engine's reservation (measured in `greybeards-scene-casts-party`).

## A player's story events (party-model.md rule 4, built 2026-10-27)

```text
a guest's character changes location on the host (TESActorLocationChangeEvent, MoveRefToNewSpace 19799 / 37313)
  StoryEvents' sink (host: HostEngine starts it) -> CopyLocationChangeEvent -> HostService -> HostAuthority
  HostStory: a guest's character -> HostGame::RaiseStoryEvent -> HostEngine -> StoryEvents::RaiseLocationChange
    CLOC queued (32288, AddEvent_Impl 32359), its id remembered with the character
a guest crafts (its own menu): BGSStoryEvent's build from the raw event (32330) sees this game's CRFT
  -> PlayerCraftEvent -> GuestWorldState -> StoryEventRequest -> HostStory -> RaiseStoryEvent -> CRFT queued, the same
the story manager processes an event (ProcessEvent 32372, hooked; StoryEvents::Judge)
  raised for a character, or a KILL whose killer (member 1) is another player's character:
    the node and quest visitors (32429 +0x19D, 32430 +0x3F) build ConditionCheckParams (29871) with that character,
    not (g_thePlayer, g_thePlayer)
  a CLOC: one per party arrival (PartyArrivals): a player arriving where another player already is is the party's
    arrival already, processed as none (repeatable WIGreeting, BardSongStarter start once)
```

The host's own player's arrival is processed the same way, so whichever player the story manager sees first in a
location starts its quests, judged for that player. Tests: HostAuthorityTests `story_tests.cpp`, ClientTests
`party_arrivals_tests.cpp`, GameTests `story_flows.cpp`; Skyrim `guest-story-starts-no-quest`,
`guest-location-change-starts-story`, `guest-craft-raises-story`.

## Messages (party-model.md rule 5, built 2026-10-27)

A message is the screen's: the story's notification, message box or help message shows on every game, a player's own
action's on that player's game (a guest's on the host goes to it, and its box is answered there). Which natives:
those whose code calls the HUD's or a box's engine function (the Screen row), so `Debug.Notification`,
`Debug.MessageBox`, `Message.Show`, `ShowAsHelpMessage` and a mod's native that shows them alike. `ShowHUDMessage` is
also called by `AddSpell` (progression comes first), `EquipItem` and `UnequipItem` (items come first) and `AddToMap`
(its "map marker added" is the screen's).

## Questions (party-model.md rule 3, built 2026-10-27)

A question about the player with no player's action behind it (a quest's timer, a load) is asked of the party. Which
natives: those returning a value whose code calls where a reference is (`TESObjectREFR::GetDistance` 19823,
`GetCurrentLocation` 19812, `Actor::HasLineOfSight` 53829, `RequestDetectionLevel` 37764, `GetLightLevel` 37775), and
getters reading a reference's position or cell (+0x54, +0x58, +0x5C, +0x60) that call nothing (`GetPositionX/Y/Z`,
`GetParentCell`). `GetWorldspace` 19816 is left out: `PlaceActorAtMe` calls it too. The router asks only when this game's
player is the call's object or an argument, and the cause is none (a player's action asks about that player, rule 2).

```text
yes/no (HasLOS, IsDetectedBy): put to every player, this game's first, each in its player's place, until one says yes
any other (a distance, a location, a cell, a position): put to one player (PartyRules::AskedPlayer), measured from
  the question's other reference, else the asking scripts' own reference up the stack (nearest player),
  else a location they name (a player in it: IsInLocation(X), a script function over GetCurrentLocation),
  else this game's player
```

The player is swapped in the frame as rule 2's is (`PlayerInFrame`); a yes/no native is run once per player (pure reads,
never latent). A bare `GetCurrentLocation() == X` in a quest names nothing: the host's player answers.

## A box's answer

A story's box with buttons shows on every game; the host's player answers it, and the others' copies close with the
host's answer (the user's choice, 2026-10-27). Generic for any latent party call:

```text
the original's game: its native waits (latent, Scope::Ran) -> relayed with its stack id as the key (RoutedCall.ReplyId)
  VirtualMachine::ReturnFromLatent (104807) for that stack (its player pressed: ScriptMessageBoxCallback::Run)
    -> CallEndedEvent -> CallEndedRequest -> every other game (NotifyCallEnded, Source: the sender's player)
each other game: a copy starts waiting -> matched to (source, key) in arrival order (PartyRules::LatentCopies)
  CallEnded -> its stack: the queued box whose ScriptMessageBoxCallback (vtable 217737, stackId +0x10) waits for it
    is answered with the result, as CommonLibSSE-NG's SelectOption does (out of the queue 406362 under its lock 406365
    by RemoveMessageFromQueue 52284, the menu hidden when none is left, then its callback); no box: the stack returns
```

A copy its own player pressed first keeps that answer (forgotten in `ReturnFromLatent`), and one that starts after its
original ended ends at once with the result. `Debug.MessageBox` is not latent (OK only): each player closes their
own.

## Tests

- Tier 1 (`ClientTests`):
  - `party_tests.cpp`: "Where a native's call runs", every destination; the progression routes.
  - `quest_reward_tests.cpp`.
  - `code_reach_tests.cpp`: the real natives' bytes from 1.6.1170 (`AddPerk`, `ModActorValue`, `ForceActorValue`,
    `DamageActorValue`, `AdvanceSkill`, `AddPerkPoints`) read for their virtual calls and writes, and the table's
    classification.
- Tier 2 (`GameTests`): the fake engine runs the same `RouteOf` (`FakeEngine::RunScript`). Its only stand-in is
  `FakeReachOf`, a table by name of what the engine's code would read. `message_flows.cpp`: a story's notification
  and box on every game, a player's own on its game, an equip no message, a box's copy closing with the host's answer
  or keeping its own player's.
- Skyrim: the step's regressions, 24 of 24 shuffled (cutscenes, dragonborn, rewards, guest scripts,
  `natives-reach-matches-ida`); `story-message-reaches-party`, `story-box-host-answers` (a standing stone's box: the
  host clicks Cancel, the client's copy closes), and the guest's moments' boxes (`relief-takes-client-relics`: the
  box is the client's now, `shrine-blesses-client`, `keyhole-asks-client`, `ring-turns-for-client`,
  `map-marker-reaches-everyone`), 7 of 7. Harness: `hud_messages` (the HUD's notifications, `TestHarness/HudMessages`),
  `box_button` (a box's button from its movie). Rule 3: `question_flows.cpp` (a distance answered by the nearer
  guest, a guard seeing any player), `timer-asks-party-position` (a quest's `PlayerRef.GetDistance(Lydia)` is the
  near client's, the host's own action's the host's). Harness: `papyrus_result` (what a queued call returned).
