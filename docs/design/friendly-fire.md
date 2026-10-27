# Friendly fire

v1.0 feature 17 (`progress.md`), built 2026-10-26 in `.worktrees/friendly-fire`. Always on: players hurt,
knock down and kill each other as NPCs would. It replaces the fixed "PvP off" rule (`join-flow.md`).

## The rule

Each player owns its own character, so the victim's game decides its health and death. A player's hit on
another player's copy is a request to that player's game, as its hit on a host NPC is a request to the host.

```
weapon hit, A hits B
A's game   the engine's hit on B's copy
           HookDamageActor -> HitRules: AskOwner
             HealthChangeEvent(copy, -damage, attacker A); the copy is unchanged
           ActorValueService -> RequestHealthChangeBroadcast
host       Relay ToOwner -> B's game (in-process when B is the host)
B's game   OnHealthChangeBroadcast -> DamageActorHealth(player, A's copy)
             the engine's damage, death and kill credit
           B's health -> its copies (pool updates); a kill -> death.md

spell or shout, A casts at B
A's game   HookAddTarget: the target is a copy, the cast is ours
             -> AddTargetEvent -> AddTargetRequest
host       Relay ToOwner -> B's game
B's game   OnNotifyAddTarget -> MagicTarget::AddTarget (the hook skipped)
             the effect's damage -> HookDamageActor with the effect applying
             -> HitRules: Apply
           a shout's push knocks B's real player down; its ragdoll streams
           to the copies (physics-sync.md, "Owners")
```

The host needed nothing new: hits (`RequestHealthChangeBroadcast`) and effects (`AddTargetRequest`) were
already relayed to the actor's owner, a player's character included.

`HitRules` (`Games/Skyrim/HitRules.h`) is the damage hook's rule with no engine, checked by ClientTests and
followed by GameTests' fake engine. Given who the hittee and hitter are on this game:

- **Our player hits another game's actor** (a host NPC, another player's copy): ask its owner. Until
  2026-10-26 a player's copy was left alone here.
- **Another player hits our player:** a replayed swing is dropped, because the attacker's game already sent the
  request. An effect being applied is taken, because it is that request arriving. Until 2026-10-26 both were
  dropped.
- Our own actors take what the world does to them. Others' actors hit by someone else are their games' to
  decide.

A hostile spell or shout on another player went no further than `IsBlockedPvp` (`MagicTarget.cpp`), now
deleted. A cast from the world (a trap, a script making the copy cast) was never blocked.

## Decisions (2026-10-26, from the user)

- **Kill credit: a named chat line only.** The engine credits the killer's copy on the victim's game, so mods'
  `OnDeath` and `GetKiller` see it. The victim's game sends the killer's session id with its fall
  (`PlayerDeathRequest::KillerId`). When that is another player's character, `HostDeath` names that player
  (`NotifyPlayerPerished::KillerName`), and every chat says "<victim> was killed by <killer>." instead of
  "has perished." There is no XP and no kill count: vanilla gives no XP for kills.
- **Not a crime, and nobody turns.** A teammate's hit is the party's own business. The victim's game runs no
  NPC reaction for its own player (`ReactToAttack` is skipped for `0x14` in `ActorValueService` and
  `MagicService`), so its followers and allies do not turn on the attacker. Shared party bounty (feature 2,
  `shared-party-bounty.md`) judges assault and murder alarms on the host. A player victim has no crime
  faction, and the host never reacts to a hit on a player for it. Check this once both are merged.

## Not covered

- **Melee stagger.** Only the hit's damage is sent, as for host NPCs today. A power bash on a player hurts
  but does not stagger it; a shout's push does knock it down (an effect).
- **A dead player's copy jitters on the attacker's game**: fixed 2026-10-26 (steered, and kept on its ragdoll by the
  engine as its owner; never placed where its owner lies); a copy getting up on its own timing stays open (`physics-sync.md`, "A copy's ragdoll, steered").
  `kill-move-kills-guest` fails on it today, as it should.

## Kill moves on players

Work as on NPCs (measured 2026-10-26, `KillMoveRandom` 100): the attacker's game pairs its player with the victim's
copy, the victim's game plays the victim half on its real player, the victim dies when the kill move ends, the
killer is the attacker's copy and the chat names it. Two bugs it found, both in the death feature:

- the revived victim's skeleton floated over its feet (its ragdoll left in the physics world): fixed in
  `PlayerCharacter::Revive` (`death.md`, "The ragdoll");
- spectating could drop into first person, in the watched copy's head: spectating is third person only now.

## Tests

- ClientTests `[HitRules]`: every hittee and hitter pair.
- HostAuthorityTests: a player's hit and spell on another player go to that player's game alone (`relay_tests`);
  a player killed by another is named in every chat, and an NPC's kill names no one (`session_tests`).
- GameTests `[friendly-fire]`: a guest's sword on the host's player, the host's on a guest's, a guest's on another
  guest's, a guest's fire bolt on the host's player, and a guest killed by another guest (real death, spectating,
  the named line). All five fail on the old rule.
- Skyrim: `friendly-fire-guest-sword`, `-host-sword`, `-guest-firebolt`, `-host-firebolt`, and
  `friendly-fire-kill` (the host kills the guest, who spectates and respawns); `kill-move-kills-guest` (the host's
  kill move kills the guest: the pair, the named death, both games' ragdolls traced, the respawned body standing);
  `spectator-watches-kill-move`.
