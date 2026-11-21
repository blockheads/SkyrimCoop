# Stealth: the HUD's eye and detection passes

Read on 1.7.104 (2026-11-08) for "the other player looking counts as being seen". Names are in the IDA database
(journal `agent-sneak-eye-looking`).

## What the eye reads

```
PlayerCharacter__UpdateStealthMeter (40593), each frame
  in combat?  eye = 100 - the combat group's search level
  else        eye = RequestHighestDetectionLevelAgainstActor(player) (41408), > 0 shown as 100
              sneak XP while someone "sees" (state +0x15) and the player moves

41408: for each actor in high process (not dead, not disabled, a process)
  judging the player, skip it if any:
    boolFlags (+0x204) kDoNotShowOnStealthMeter (bit 26)   MQ201 sets it on Malborn and co.
    boolBits (+0xE8) kPlayerTeammate (bit 26)              followers
    boolFlags kIsCommandedActor and its commander is the player   summons
  state = Actor__GetFreshDetectionStateOf(actor, player, priority 3) (37758)
        = ActorKnowledge__GetFreshDetectionState (42702) on its knowledge of the player
  highest = max(highest, state.level); "seeing" += state +0x15
```

So any other high-process actor's knowledge of the player counts, another player's copy included, unless flagged.
`Actor__SetNotShowOnStealthMeter` (37722, Papyrus `Actor.SetNotShowOnStealthMeter` 54969) flips that one bit.

## How a knowledge gets a state

```
42702 GetFreshDetectionState(knowledge, priority)
  no state                       -> request (42712), answer none
  a request queued (+0x44 set)   -> answer the state as it is, whatever its age
  older than max (prio 3: 2 s)   -> request, answer none
  older than refresh (1 s)       -> request, answer the state

42712 RequestDetectionUpdate: only when nothing is queued: stamps +0x44, queues the knowledge

DoDetectionJob (42742), each queued knowledge in turn
  state = pooled copy of the old one
  level = CalculateDetection(...) when detection is on and both within 8192 units, else -1000
  DetectionState::Set (42760): level, hasLOS (+0x14), seen (+0x15), +0x16; UpdatedAt (+0x38) = now
  ActorKnowledge__SetDetectionState (42704): installs it, clears +0x44 (-FLT_MAX)
```

A second writer skips the pass: `Actor__ForceDetected` (37765) sets a witness's state of a target to 100 through
`ActorKnowledge__SetDetectionStateKeepQueued` (42705), which leaves +0x44 as it is. Its callers: `TESNPC::Activate` and
`MenuTopicManager::EndDialogue` (the actor talked to detects the player), pickpocket and steal alarms. On a knowledge
already queued, that 100 answers at any age. Read in IDA; on the duo, talking to or pickpocketing a player's copy is
refused, so no co-op path to it was measured.

## Our hook

`HookUpdateDetectionState` sits on 42704. It used to return without calling the engine when another player's copy
judged this game's player. That left the knowledge's request queued forever: 42712 never queued another, and 42702
answered whatever state the knowledge held, at any age. Measured on the duo (2026-11-08), standing and sneaking, both
games: the copy's knowledge of the player read `queued`, no state, so it added nothing. Read in IDA, not measured: a
level installed before the hook skipped would stay on the eye for good. How one would get there (a copy judging before
its flags, as after a cog swap resets the extensions) is inferred; no positive state was seen to survive.

Now the hook rewrites the pass to unseen (`DetectionRules::Installed`: level -1000, no LOS, not seen, the fields a pass
with detection off writes) and lets 42704 install it. The request clears, the state is fresh, and 42702's freshness
rule runs as for any NPC.

That still left 42705 and any older level. So a player's copy is also flagged `kDoNotShowOnStealthMeter` when it spawns
(`CharacterService`, `SetActorNotOnStealthMeter`): 41408 skips it before reading its knowledge, whatever that holds,
as MQ201 does for its allies. Tests: `detection_rules_tests.cpp`, GameTests `[stealth]` (a planted level, a forced
detection), scenario `other-player-looking-leaves-eye-hidden`. The harness's `crime stealth_meter` lists each high
actor's state of the player (`sources`) with its flag, `crime knowledge` the flag (`not_on_meter`).

Measured on the duo (2026-11-09, dev's hook): talking to or pickpocketing the other player's copy is refused, and the
eye stayed closed on screen with the copy facing the sneaking player from 120 units. The playtest's "detected" was not
reproduced; its `/bug` reports caught no sneaking moment.

Head tracking reads the same state: `ProcessTracking` (38009) drops a look at this game's player its tracker does
not detect (37758). The unseen state above made a player's copy drop its owner's look at this game's player, so a
copy's tracking does not ask (`actor-ai-and-copies.md`, "A look at this game's player needs detection").

## Line of sight (read 2026-11-21)

Read in IDA on 1.7.104 for piece 8 (`one-simulator.md`, open question 7); journal `agent-piece-8`.

```
CalculateDetection 37774 (detector D, target T)
  D in combat, T not the player and not D's combat target (+0x104)
      -> range x fDetectionCombatNonTargetDistanceMult (0.75)
  CalculateLOS 37768 (only inside that range)
      T more than half fDetectionViewCone (190) off D's facing (37773)  -> none
      T the player, D's combat target, or D the player -> rays to T's torso, head, feet (37771)
      anyone else                                      -> one ray, to its head
      ray: layer kLOS (41), D's capsule group; seen if it hits T's 3D (19750) or nothing
  0x14041CD90: sound (T's noise +0x11C, movement, weight) + sight (needs LOS and the cone) - sneak
```

Measured on the duo (harness `sight`, MCP `sight_watch`; Orgnar with the host at +80, the guest at -80): no ray to a
guest's copy ever hit anything. Its line of sight was lost only when it stood outside Orgnar's cone (167-180 degrees
off, Orgnar facing the host): vanilla geometry, nothing a copy changes.

## A player's noise (read 2026-11-21)

```
Actor::SetActionValue 37711: actionValue (+0x11C) = noise, timerOnAction (+0x120) = fActorAlertSoundTimer (5 s)
  PlayerCharacter::Update: its controller bumped a character (bumpedCharCollisionObject, 78291), not a teammate
      -> iSoundLevelLoud (100); also a weapon's hit frame (38603), TESObjectWEAP::Fire, a cast (0x1405CDF50)
  counted down by PlayerCharacter__UpdateTimers (0x14075FA50) for the player, Character::UpdateAI (0x14067A046)
  for anyone else; at 0 the noise clears
```

Only the real player runs the bump rule, on its own game: a guest walking into an NPC is loud on its game and was
silent on the host. Measured (duo, the inn): at the host's death Orgnar heard the host's player (noise 100, sound term
187, level 210-228) and not the guest's copy beside him (noise 0, level 16-19, sight alone), while the guest's own game
read its player at 100 and level 215. The copy, the new target, was then searched for 2-11 s every round: "A still guest
unseen up close" (bugs.md, 2026-11-06).

Now the noise is its owner's (one-simulator.md, R2): `ActorStatePart::Noise` carries the owner's actionValue, its copy
holds it until the owner's part clears it (`ActorStateRules::CopyNoiseTimer`; the copy's own countdown never ends it),
and `HookSetActionValue` keeps a copy's own engine (a replayed swing or cast) from setting one. Five rounds of
`host_death_watch` after: no search, the copy at 175-209 at the switch. Tests: GameTests "A guest's bump is heard on
the host", ClientTests `[actor-state]`, TPTests the part's round trip, scenario `guest-bump-heard-on-host`.

## The eye is the host's (read and built 2026-11-21, one-simulator.md piece 8)

```
40593 the eye: in combat (vfunc 0xE3) 100 - CombatManager__GetLowestStealthPointsOf (46877: the lowest over every
               combat group, any actor; none: 100); else 41408's level, > 0 shown as 100; sneak XP while seeing > 0
41408's other callers read the player's level as > 0 (an assault alarm 0x1406F1F50, lockpicking 51981, an activator's
      trespass) or add it to a modifier (the stolen item owner's 41382)
42702: a state older than 2 s (priority 3) answers none and queues a pass
```

Measured on the duo (`crime stealth_meter` with `actor`, MCP `eyes`): the guest's eye was its own game's. Its NPC
copies judged its player in their own passes, and its game has no fight of theirs: in a fight with Andurs its eye
showed 0 (46877's default) while the host's Andurs had the copy at stealth 0 (shows 100). And the host's NPCs' state
of a guest's copy was stale: nothing asks it each frame as the HUD asks of the host's player, so every read a few
seconds apart found it older than 2 s and answered -1000, though Andurs's pass computed 31.

```
host, each frame (HostEye, HostEngine::ReadEye -> StealthEye::Read): per guest's copy 41408 and 46877, asked as the HUD
      asks of its own player (keeping its NPCs' state of the copy fresh)
      changed (DetectionRules::EyeChanged: seen or not, seeing or not, the level by 5, a stealth point)
      -> NotifyStealthEye to that guest only
guest: GuestEye -> GuestEngine::HoldEye -> StealthEye's hooks on 41408 and 46877 answer it for this game's player
every game: a copy's pass of this game's player is unseen (DetectionRules::Installed, any copy now)
```

Measured after: in the fight both games show 100 (scenario `guest-eye-is-hosts`); sneaking in front of Andurs the
guest's eye follows the host's reading a read behind (-14, then 47). Tests: GameTests "A guest's eye is what the host's
NPCs see of it", HostAuthorityTests `[Eye]`, ClientTests `[DetectionRules]`, TPTests the message.

| ID | Name | Use |
|---|---|---|
| 40593 | PlayerCharacter__UpdateStealthMeter | the HUD's eye |
| 41408 | ProcessLists::RequestHighestDetectionLevelAgainstActor | highest fresh level of the player |
| 37758 | Actor__GetFreshDetectionStateOf | an actor's fresh state of a target |
| 42702 | ActorKnowledge__GetFreshDetectionState | freshness rule |
| 42704 | ActorKnowledge__SetDetectionState | installs a pass; `HookUpdateDetectionState` |
| 42705 | ActorKnowledge__SetDetectionStateKeepQueued | 37765's install |
| 37765 | Actor__ForceDetected | level 100 without a pass |
| 37722, 54969 | Actor__SetNotShowOnStealthMeter, Papyrus | the eye's skip flag |
| 42742 | DoDetectionJob | answers queued knowledges |
| 42760, 42761 | DetectionState::Set, Clear | a pass's fields |
| 37768 | Actor::CalculateLOS | rays from the detector's eye (one, or three for the player) |
| 37771, 37770, 37773 | Actor::CalculateLOSLocation, CalculateLOSToPoint, GetAngleToPoint | its points, a ray, the cone |
| 37711 | Actor__SetActionValue | an actor's noise; `HookSetActionValue` |
| 0x14041CD90 | Actor__ComputeDetectionLevel | sound + sight - sneak |
| 41408 | ProcessLists::RequestHighestDetectionLevelAgainstActor | `StealthEye`'s hook: the host's on a guest |
| 46877 | CombatManager__GetLowestStealthPointsOf | the eye in combat; `StealthEye`'s hook |
