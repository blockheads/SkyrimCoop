# Compass enemy markers

Read in IDA on 1.7.104, 2026-11-24 (the bug: a guest's compass showed no red enemy dots; the host's did). Community
source first: [Compass Navigation Overhaul](https://github.com/alexsylex/CompassNavigationOverhaul), whose
`UpdateEnemies` hook sits on the same call (`HUDMenu::ProcessMessage` 51612 +0x1735).

## The path

```
Main 36592, once a frame, after the CombatManager update
  PlayerCharacter__UpdateActorsToDisplayOnHUD (41240)
    actorsToDisplayOnTheHUD (PlayerCharacter +0x9E8 on 1.7.104, a BSTArray of handles) cleared
    each CombatManager group with the player among its targets (CombatGroup__HasTarget 44706):
      every member of the group (group+0x20, 12 bytes each) appended
HUDMenu__ProcessMessage (51612)
  the first 16 of that array -> HUDMarkerManager__AddMarker (51728), enemy frame
Compass::Update (51668) -> SetCompassMarkers
```

- Read: 41240 has no other input. A red dot is a member of a combat group that targets this game's player; detection,
  hostility and distance are not tested (51728 refuses only past 48 markers and the player itself).
- Read: group targets at group+0x08 (`CombatTarget`, 0xA8 each, count +0x18), members at +0x20 (count +0x30).

## Copies

A copy has no combat group (`Actor::StartCombat` 38561 is refused for copies, combat-targets.md), so on a guest no
group targets the player and the array stays empty: the bug. The host's NPCs are in groups on the host only.

Measured (duo, 2026-11-24, scenario `compass-enemies-on-guest`): a wolf the host hit showed on the host's compass and
not the guest's. Before the hit, the wolf's own target was Lydia while the host's compass showed it: its group
targeted the host. A copy's own combat target is not the test; the group's targets are.

## The fix

```
owner's game                                  each game
CombatGroup__Update (44703) hooked:           AI state record arrives
  targets changed (a hash per member,           copy in combat, GroupTargetPlayers holds
  ActorExtension::GroupTargetsSent)             this game's player -> compass list
  -> AiStateChangeEvent per member            PlayerCharacter__UpdateActorsToDisplayOnHUD
AI state read: GroupTargetPlayers =             (41240) hooked: the list appended
  the players among the group's targets
```

- The owner's AI state carries the players its combat group targets (`ActorAiState::GroupTargetPlayers`, session
  ids; `IActorBridge::GetActorAiFields` reads the group's targets under its lock).
- Read in IDA: 44703 runs on the main thread, once a frame per group, from `CombatManager__UpdateGroups` (46869); it
  adds and drops targets with no event, so its hook compares the targets with what each member last sent.
- Each game: `AiStateService` lists the copies in combat whose group targets this game's player
  (`IActorBridge::SetCompassEnemies`), and the 41240 hook (`Games/Skyrim/CopyAiState.cpp`) appends them, as 41240
  appends a group's members. A player the host's group has not detected gets no dot, as in vanilla.
