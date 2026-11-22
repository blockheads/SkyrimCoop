# Dev commands: engine path

What the dev chat commands rely on (`docs/design/dev-commands.md`). 1.7.104, read in IDA 2026-11-16 unless measured.

## Kill moves have no chance (read)

- The kill move root idles (Skyrim.esm `KillMoveFrontSideRoot` 24CD4, `KillMoveBackSideRoot` E8458 and their `00`
  twins) need `GetGlobalValue KillMove` (100F19) == 1, `ShouldAttackKill` == 1, `IsLastHostileActor` == 1, plus angle,
  attack state and weapon conditions. No `GetRandomPercent` at the root; the per-variant ones only pick a clip.
  `KillMoveRandom` (GLOB 5159D, 50) is read by no condition in the four masters and by no code.
- Condition opcodes from the script function table: `ShouldAttackKill` 0x12A6 (index 678), `IsLastHostileActor`
  0x12A2 (674), `GetRandomPercent` 0x104D (77).
- `GameFunc::native::ShouldAttackKill` (21744): 1.0 when subject and target are actors and `Actor::WouldAttackKill`
  (38559, named here) returns true: a probe `HitData` (flag 0x2000) for the attacker's current weapon, its fatal flag
  (0x20).
- `GameFunc::native::IsLastHostileActor` (21743): 1.0 when `ProcessLists::AreHostileActorsNear` finds none, or exactly
  one and it is the subject.

- `Actor::WouldAttackKill` (38559) in full (read 2026-11-22): false when the target is dead (vfunc 0x99,
  `Actor::IsDead(false)` 37483: with false an essential-down actor, life state 7, counts as dead; the Papyrus native
  passes true) or not `Actor::CanBeKilledBy(target, attacker)` (37229, named here: reanimated, or not essential
  `+0x204` 0x40000, not a child (vfunc 94, on `Character` 40272: race flag 4), not protected 0x80000 unless the
  attacker is the player); then the probe hit's fatal flag, false again when
  the attacker's process byte (39996: middle-high `+0x324`, meaning not read) is set and it is not `EngagingInCombat`
  (38563). No random number anywhere on the path.
- The target reaches the condition (read, `TESConditionItem::IsTrue` 29924): the idles give both conditions param 0;
  an empty param1 of a reference type (`coop_ConditionParamIsReference` 0x1404A8DC0: param types 4, 6, 24, 26) becomes
  the check's `targetRef`, and the function is called as (subject, param1, param2, &result).
- `fKillCamBaseOdds` and its level bias (26452, `coop_KillCamOddsRoll`) roll a kill cam from the two levels, but
  nothing calls it (no rel32 call or jump, no stored pointer): not part of the pick.

`KillMoveOdds` hooks both condition functions and rewrites the result only while odds are set: `ShouldAttackKill`
answers the 38559 checks before its probe (dead, 37229, the combat gate) and a roll under the odds, fatal or not;
`IsLastHostileActor` answers yes. The roll is kept per attacker and target for a second (`DevCommands::KillMoveRolls`):
the idle tree asks the conditions many times a swing.

**Why `/killmove 100` did nothing (2026-11-22).** It kept the fatal test (fatal and a roll), so a kill move still came
only on a killing blow, and the game's own rule already plays one on most killing blows on a lone enemy (for the player
the last-enemy test is the only other gate). Measured on the duo at 100: three swings at a 1000-health wolf were three
plain `attackStart`s; a 100-health wolf's kill move came on its second, fatal swing. With the fix one swing at the
1000-health wolf is `pa_KillMove1HMWolfA` and it dies, on the guest and on the host (scenario
`killmove-odds-every-swing`). The 2026-11-16 check ("at 100 the guest's idle tree picked `pa_KillMove1HMWolfB`") used
fatal swings, so it could not tell.

## Gravity and jump (read)

- `bhkCharacterStateInAir::Simulate` (80098, named here): while falling, the gravity mult is the INI setting
  `fInAirFallingCharGravityMult:Havok` (default 1.35, read live each step); rising it is 1.0. Velocity += world gravity
  (`bhkWorld` 77838) * mult * controller gravity scale (+0x248) * dt.
- `fJumpHeightMin` (GMST, 76 in Skyrim.esm; 64 in the executable) is read at each jump: `JumpAnimEventHandler::Handle`
  (42892) passes `fJumpHeightMin * scale` (times another GMST in character state 5) to `bhkCharacterController::Jump`
  (78262, named here); `Actor::Jump` (37257) does the same for its own jump.

So `/gravity x` scales the fall by x and the jump height by 1/x (rising gravity stays the world's). Measured: `getini`
0.41 and `getgs` 253.33 after `/gravity 0.3`, in both games.

## Speed (inferred)

`/speed` sets `SpeedMult` and then changes carry weight up and down. The carry-weight step is the community's way to
make a new `SpeedMult` apply (UESP, "SpeedMult"); it is not read here. What was read: `ActorValueList::InitActorValues`
gives `SpeedMult` (0x1E) and `CarryWeight` (0x20) no callback, so nothing re-reads `SpeedMult` when it is set.
Measured: `SpeedMult` read back 200 in both games after `/speed 2`; the walking speed itself was not measured.

## God mode (read)

The console's `tgm` (22814) is `PlayerCharacter::SetGodMode(!IsInGodMode())` (40500, 40501), one byte. `/god` calls
the two directly: `/god on|off` sets it whatever was on (the console, `profile --load`, a game swapped in) and `/reset`
clears it.

## killall (read)

The console's `killall` (`ConsoleFunc::KillAllActors` 441586) queues a kill (`Actor::QueueKill`) for every actor in
`ProcessLists::highActorHandles`: players' copies and followers too. `/killall` uses `DevToolsBridge::KillHostiles`
instead: high actors hostile to this game's player (`Actor::IsHostileToActor` 37537), players and copies skipped.

## The console (read before: console.md)

`ConsoleLines::Execute` is `Console::ExecuteCommand` (442672): it queues, and the lines run later in the frame.
Measured: the 141 lines of `/shouts` queued in one frame all ran (the last, `shoutrecoverymult 0`, read back 0).
`player.additem` of a leveled item list rolls it (LootBanditChestBossMagicItem100 E88: three enchanted items for 3).

## Found on the way

A guest's fatal swing at a harness-frozen host NPC: the host refused every forwarded hit as "held by a kill move"
from then on, at vanilla odds too (`docs/bugs.md`, "A frozen NPC a guest's kill move missed").
