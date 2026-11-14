# Combat aim: where an NPC's arrow goes

Read in IDA on 1.7.104 (names and comments in the database, journal `agent-fanedall-misses.tsv`) and measured on the
duo, 2026-11-14, for "Faendal's arrows keep missing the guest".

## The engine path

```
CombatProjectileAimController::Update (44384, vfunc 8 of VTABLE 210081)
  held while flags 0x40 set and 0x400 clear; else
  UpdateAimSolution (44396):
    offset = GetTargetLocation(target, kind 2/3/4) - target position     (0x140866D90, Actor 37771: a body point)
    t = 0; repeat (converges, game setting count):
      t = time of flight to the point (distance / speed +0x50, or the arc solver with gravity) + launch delay +0x98
      point = Actor::GetPredictedPosition(target, t) + offset              (47309)
    P (+0x18) = point; a random accuracy offset (+0x8C, scaled by +0xA0)

Actor::GetPredictedPosition (47309)
  t <= 0: the position
  the target's path follower if it answers (90724), else position + GetLinearVelocity (vfunc 0x86) * t
  a guess more than 32 units off (dword_1420A1690): Actor__CheckPredictionOnNavmesh (0x14086A000) walks the
    navmesh toward it and may pull it onto the navmesh; no navmesh place found: the guess is the position

TESObjectWEAP::Fire (18102), an NPC in combat (0x14028C8AE)
  CombatController::GetAimPoint (0x1405629F0): the aim controller whose source (+0x10) is this weapon, its P
    (no aim: the shot is queued instead)
  heading, pitch = VectorToHeadingPitch(P - fire node)                     (0x14086B8B0)
  -> LaunchData angles -> Projectile::Launch (44108); the projectile takes them as they are (Projectile ctor
     0x1407F8A50: SetRotationZ/X/Y)
```

The combat controller is at `Actor+0x160` on 1.6.629 and later (CommonLib-NG's `0x158` plus 8); its aim controllers
at `+0x50` (`CombatController.h`, `CombatAimController.h`).

## In a session

- **The host runs the aim.** The host owns Faendal; the guest's player is a copy on the host, so 44396 aims at the
  copy. Measured (`aim_watch`): still, P sits 1-7 units from the copy, at chest height (~100 above its feet); strafing,
  the copy's `GetLinearVelocity` reads its owner's speed (about 330 units/s), so the lead is right for the copy.
- **The guest judges the hit.** `CombatService` sends the launch (origin and the final angles) and the guest's game
  launches the same arrow from Faendal's copy (`SkyrimBridge::LaunchProjectile`); `HitRules` applies a copy's hit on
  the local player there. Measured: the statics both games' arrows hit are the same references, so the path is the same.
- **So the arrow is aimed at where the guest was.** A copy shows its owner as far behind as its clock needs
  (`copy-clock.md`: 60-80 ms on a good line, plus the line's delay), and the arrow takes the line's delay again to reach
  the guest's game. The guest is `speed x (copy delay + line delay)` past the aim: 50-60 units for a running strafe on a
  60 ms line, wider than a body.

Measured, Faendal 750-800 units off with his hunting bow, the target strafing 1.5 s each way, hits on the target's own
game (one run each, small samples):

| Target | Line | Arrows | Hits |
|---|---|---|---|
| the host's own player (vanilla's case) | none | 12 | 3 |
| the guest | none (in-process) | 11 | 4 |
| the guest | 60 ms each way | 12 | 1 (the host saw 2 on its copy) |

Seen while staging, not the bug: a still guest at first went to combat state 2 (search) and the fight ended, the open
"copy unseen" detection (`combat-targets.md`); a static between them or the other player in the line takes the arrows.

## The fix

`HookGetPredictedPosition` (47309, `CopyAiState.cpp`): for a copy and `t > 0`, the engine's own guess, then
`+ GetLinearVelocity x lead`, with `lead = CopyLead::Ms(how far behind its owner the copy shows, CopyClock::Network())`,
set each frame by `CharacterService::RunRemoteUpdates` (`IActorBridge::SetCopyLead`, `ActorExtension::PredictionLeadMs`).
So the guess lands where the owner will be when a hit reaches its game.

The lead is added after the call, not to `t`: a longer `t` sends more guesses through the navmesh check, and a failed
check drops the whole guess to the copy's place. The copy's lag is where its owner already stands, so it needs no
check of its own; the engine's check keeps vanilla's horizon. It is generic: a melee swing's range check
(`CheckAttackInRange`, 47309 at the strike time) and close movement (47309 at 0.5 s) read the same guess, and their hits
are judged on the owner's game too.

Measured on the duo (`expect_aim_leads`, 60 ms each way, the guest strafing 2 s each way, only guesses whose window
holds no turn; the copy read 120-220 ms behind its owner, the line 70-120 ms):

| Build | Spot | Median off | Along the guest's way |
|---|---|---|---|
| no lead | A | 117 (reads stamped late; see below) | behind |
| `t += lead` | A | 26 | |
| `t += lead` | B | 220 | -220, every guess collapsed |
| lead after the call | B | 97 | -67, guesses collapse for about a second a leg |

The first runs stamped both reads after both round trips (100-200 ms each), which reads as the guess trailing; each
read is stamped at its own round trip's middle since. Spot B's collapses are the engine's navmesh check failing for
the copy and not for the host's own player in the same field: open, `bugs.md`, "A copy's predicted place falls back
to where it stands".

The alternative, the host judging its NPC's hits on a copy, would hit a guest that saw the arrow miss; this keeps the
hit where the player sees it.

Tests: GameTests "A host NPC's aim at a running guest's copy leads to where the guest will be" (the fake's
`PredictedPosition` is 47309's position + velocity x t; mean 51 units off without the lead, 6 with it), ClientTests
`[combat-aim]`, scenario `faendal-aim-leads-guest` (fails at spot B on the collapse above).

Tools: harness `combat_targets` reads the target's `target_velocity` and the bow's `aims` (P, flags, speed, delay);
MCP verbs `aim_watch` (P against the guest, hits per game), `provoke`, `expect_aim_leads`, `copy_lag`.
