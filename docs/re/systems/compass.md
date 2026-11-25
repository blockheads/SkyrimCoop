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
not the guest's. Before the hit, the wolf's own target was Lydia while the host's compass showed it. That its group
targeted the host is inferred from 41240/44706 (the group's targets were not read). A copy's own combat target is not
the test; the group's targets are.

## The fix (2026-11-25)

A copy holds a real controller and group filled from its owner's fight (combat-targets.md, "A copy's combat"): the
owner's AI state carries its group (a key unique across games) and the session actors among its targets, and the
copy's group holds the same targets. 41240 then lists the copy natively when its group targets this game's player.
No hook on 41240.

- Read in IDA: 44703 adds and drops targets with no event; its hook (`AiStateEvents`, behind `WorldGate::Reader`,
  since it runs on the combat job: `Job_Combat_manager` 39044 -> 46869) compares each group's targets with what its
  members last sent and marks them changed.
- Measured on the duo (spike, 2026-11-25): with the 41240 bridge off, the guest's compass showed the wolf once the
  copy's group held the guest's player; before the copy had a group, it showed none.
- A player the host's group has not detected gets no dot, as in vanilla.
