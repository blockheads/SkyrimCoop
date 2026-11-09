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
  state = Actor__GetFreshDetectionStateOf(actor, player, priority 3) (37758)
        = ActorKnowledge__GetFreshDetectionState (42702) on its knowledge of the player
  highest = max(highest, state.level); "seeing" += state +0x15
```

So any high-process actor's knowledge of the player counts, another player's copy included.

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

## Our hook

`HookUpdateDetectionState` sits on 42704. It used to return without calling the engine when another player's copy
judged this game's player. That left the knowledge's request queued forever: 42712 never queued another, and 42702
answered whatever state the knowledge held, at any age. Measured on the duo (2026-11-08), standing and sneaking, both
games: the copy's knowledge of the player read `queued`, no state, so it added nothing. But a level installed before the
hook skipped (a copy judging before its flags, as after a cog swap resets the extensions) would stay on the eye for good.

Now the hook rewrites the pass to unseen (`DetectionRules::Installed`: level -1000, no LOS, not seen, the fields a pass
with detection off writes) and lets 42704 install it. The request clears, the state is fresh, and 42702's freshness
rule runs as for any NPC. Tests: `detection_rules_tests.cpp`, GameTests `[stealth]`, scenario
`other-player-looking-leaves-eye-hidden`.

## Not covered here

- An NPC copy on the guest judges the guest's player itself (its own pass, measured: Lydia's copy, level -21, fresh).
  That is the guest's estimate, not the host's number (`crime.md`; `docs/bugs.md`, "The guest's eye is its own
  estimate").

| ID | Name | Use |
|---|---|---|
| 40593 | PlayerCharacter__UpdateStealthMeter | the HUD's eye |
| 41408 | ProcessLists::RequestHighestDetectionLevelAgainstActor | highest fresh level of the player |
| 37758 | Actor__GetFreshDetectionStateOf | an actor's fresh state of a target |
| 42702 | ActorKnowledge__GetFreshDetectionState | freshness rule |
| 42704 | ActorKnowledge__SetDetectionState | installs a pass; `HookUpdateDetectionState` |
| 42742 | DoDetectionJob | answers queued knowledges |
| 42760, 42761 | DetectionState::Set, Clear | a pass's fields |
