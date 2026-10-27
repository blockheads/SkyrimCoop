# NPC followers

Shared vanilla followers with a member as their anchor, approved 2026-10-27.
Implemented on dev; changes are uncommitted for review.
NPC simulation and the world remain the host's; a follower's trading partner is the player who picked the line
([party-model.md](party-model.md)). A player following the travel tether is a separate concept.

## What vanilla does

Measured on AE 1.6.1170 with two games, using the guest's real dialogue menu and its recruitment choice on a
host-created Lydia. Evidence: `follower-measurement.log` and `follower-records.log`, preserved in
`build/followers-review/`.

```text
guest activates Lydia's copy and picks "Follow me. I need your help." (topic B0EE6)
    -> host runs the picked line's fragment with the guest's cause
    -> DialogueFollower (750BA), Follower alias 0: empty -> host Lydia FF00094E
    -> PlayerFollowerCount (BCC98): 1
    -> PlayerFollowerFaction (84D1B): rank 0
    -> PlayerFollowerPackage (5C84B): package_target = follow_target = 14
                                                        ^ host's player
       guest's player copy on host: FF00093A             ^ not this copy
```

Recruitment therefore already reaches the host's quest. Rule 2's script context does **not** retarget the
package's stored player reference. This is a measured distinction, not evidence that guest recruitment needs
another replay. The follower remained an actor owned by the host. Dismissal through the existing harness's
call to `DialogueFollowerScript.DismissFollower(0, 0)` cleared the count; the temporary Lydia was removed.

Read from the installed vanilla `Data/Scripts.zip` and Skyrim.esm:

- `DialogueFollowerScript.SetFollower` sets teammate state, relationship to `Game.GetPlayer()`, the Follower
  alias and the count. It does not choose a per-member follow target. The quest has one Follower alias and
  one Animal alias; `PlayerAnimalCount` is 5C84A.
- `SetAnimal` sets relationship and teammate state, fills Animal and increments its count. Its teammate call
  disables favors. Dog commands must preserve that distinction.
- Wait sets `WaitingForPlayer` and registers a 72-game-hour update. `FollowerAliasScript` dismisses a waiting
  follower at that update, handles death and clears the alias/count. Follow clears the waiting value.
- Dismissal clears teammate/hireling state, removes the vanilla hunting bow/arrows and clears the alias/count.
  Hireling scripts charge gold and then call the same `SetFollower` path. The party's purse must pay once.

Native acceptance uses real activation and dialogue choices. Coverage and outstanding checks are listed below.

## Choices

| Option | Ownership and follow target | Tradeoff |
|---|---|---|
| Keep vanilla targeting | Party owns the follower; it follows the host | Smallest change; a guest who recruits cannot take the follower with their character while walking apart |
| Shared followers with a member as their anchor — recommended | Party owns the follower; host simulates following the recruiter or the latest player who asks it to follow | Keeps vanilla slots and quests, while making guest recruitment useful; needs explicit package-target handling |
| A separate roster for each member | Each member recruits their own follower set; host simulates every NPC | More companions, but requires replacing vanilla aliases/counts and resolving quest/mod assumptions; defer beyond v1 |

Recommend the second option, retaining vanilla's one humanoid and one animal slot for the whole party.
A housecarl belongs to the world's existing quest/ownership state; recruiting it does not create a guest-owned
NPC or another copy of it. Temporary quest companions continue to use their own quest's rules.

## Approved behavior

```text
member's recruitment / "follow me" choice
    -> existing host dialogue and vanilla follower quest
    -> party follower binding: NPC + member's stable player key
    -> native follow target on host: that member's current actor/copy
    -> host runs packages, combat, inventory and movement
    -> other games show the usual NPC copies

member picks "trade"
    -> host authorizes one inventory interaction for that NPC
    -> picker sees follower inventory on their own game
    -> transfers connect follower's host inventory <-> picker's own inventory
    -> existing inventory replication updates everyone
```

- **Recruiting and following:** recruitment establishes the anchor. Any member may ask a party follower to
  follow them, wait, perform a supported favor, trade or leave. A follow command changes the anchor to its
  picker; trading alone does not. The host orders competing commands once. For v1, keep vanilla’s
  **Wait here → Follow me** sequence to switch the member being followed (user decision, 2026-10-27).
- **Package targeting:** resolve only the active party follower's follow target to its member's current
  reference on the host. Preserve waiting, dismissal, furniture, combat, scenes and quest override packages.
  Do not globally replace player references or treat all NPC movement as party travel. The mechanism must
  be verified against the actual follow-package target path before implementation; the current script rules
  cannot supply this by themselves.
- **Inventory and trading:** items belong to the NPC until transferred. Use the picker's inventory and the
  party's existing gold rules. Serialize overlapping inventory interactions per NPC so an item is transferred
  once; closing a menu or losing a connection releases the interaction. Do not mirror the NPC inventory into
  every player's personal inventory.
- **Combat and favors:** native AI runs on the host against the actual actors there. Preserve vanilla
  hostility, death and essential/protected rules. NPC attacks do not grant unused player skills experience.
  Commands that use the player's position use the command's member; dogs keep vanilla's favor restriction.
- **Waiting and dismissal:** retain vanilla waiting location, game-time timeout, dismissal, rehire and death
  cleanup. Shared fast-travel time must count toward the waiting timeout. A wait order never becomes a tether
  teleport for the NPC. Dismissal removes the binding only when vanilla actually clears the follower.
- **Travel:** a following NPC uses the host's ordinary follower packages and travel. Its actor is never made
  into a player or an independent leader of the party tether. Establish live travel coverage for the anchor
  being a guest; player-tether tests alone do not prove NPC follower travel.
- **Disconnect, death and rejoin:** keep the desired anchor as a stable player key. While unavailable, follow
  the standing host, or the nearest standing member if the host is down; ties use stable player keys. With no
  standing member, leave behavior to the party wipe/load. Rejoining resolves the key to the replacement copy.
  Restore the original anchor unless another member issued a follow/wait/dismiss order in the meantime.
- **Saves:** quest aliases, NPC state and inventory belong to the host save. Persist only the extra binding
  with that world, never in a guest's character record. Loading a host save restores its bindings and vanilla
  quest state together. A guest rejoin never re-runs recruitment, charges another hire fee or spawns another NPC.

```text
on host observes successful vanilla recruitment(npc, causedBy):
    bind(npc, stableKey(causedBy), Following)

on accepted follower command(npc, picker, command):
    let vanilla perform command once
    if command is Follow: bind(npc, stableKey(picker), Following)
    if command is Wait: preserve anchor; record Waiting
    if vanilla clears alias or npc dies: erase binding

on anchor actor replaced, member leaves/returns, or host world loads:
    resolve saved key to current host-side actor
    choose temporary standing-party fallback if unavailable
    evaluate the appropriate native package without replaying recruitment
```

## Implementation

`HostFollowers` reads the host's two vanilla aliases and binds successful recruitment/Follow commands to the
picker's stable player key. Its pure `Followers::Anchors` chooser resolves current player actors and standing-party
fallbacks. Waiting leaves the saved desire intact and suppresses following; the next Follow command chooses
its picker. Clearing an alias or follower death removes its binding. Host save co-record `FOLL` version 1 stores
host reference IDs and stable member keys, with SKSE reference remapping on load; it stores no temporary fallback.

`FollowerTargets` observes native teammate and WaitingForPlayer writes using the existing script cause. It
changes the active vanilla follow package's specific-player reference on the host. Rebuilding that package's
cached procedure is needed when the selected actor changes; it is guarded against combat and other packages.
See [native contracts and evidence](../re/systems/followers.md).

Follower trading is serialized per NPC by the host. `FollowerService` closes the picker's dialogue before opening
its follower container, and releases the interaction on close, failed opening or disconnect. Existing inventory
replication moves items between the NPC and the picker's inventory. It does not change the anchor.

Native findings also have GameTests regressions: temporary NPCs already in the host's loaded save must
survive its session connection; guests must discard late-loading unowned temporary NPCs; activation of an
NPC copy must resolve its session ID to the owner's actor.
The latter lets activation-based animal recruitment reach the host instead of sending a local copy's form ID.
Only the NPC owner replays it. Actor activations validate the sender owns the activator and the target is a host NPC.

No Papyrus, cutscene, message-native or story-event hooks were changed. The implementation reads the existing
script cause and lets vanilla own recruitment, slots, waiting, dismissal, favors and combat.

## Acceptance and limits

Implementation gate before integration: `followers-dev-gate.log` (433 cases plus UI fixtures and DLL),
`followers-dev-network.log` (all four follower flows under both loss/jitter profiles).
The final native run is `followers-dev-native.log`: nine follower cases pass outright; the door's behavior
passes and its audit reports the reviewed ForceRefIntoAlias -> ClearRefAlias load-order variant.
`followers-dev-door-reviewed.log` passes that case after reviewing the exact pair: ten follower native cases
pass in total. The ordinary riding regression still fails an AI-package snapshot comparison, recorded in
[known bugs](../bugs.md#party-travel).

Fast coverage includes desired-anchor selection, death/disconnect/return, nearest-standing selection and ties,
newer Follow orders, two independent vanilla slots, alias cleanup, record validation/remapping, exclusive trading,
and a three-player flow checking personal inventories and anchor changes. The native suite is `follower-*` in
`scripts/coop_followers.py`. Implementation logs below are preserved in `build/followers-review/`;
integration checks are in `build/followers-integration/`.

- Real guest recruitment and walking: `followers-live-core3.log`.
- Host and guest Wait → Follow switches, guest dismissal: `followers-commands-native.log`.
- Guest item transfer, host trade after close, unchanged anchor: `followers-cache-native.log`.
- Disconnect fallback and replacement-copy return: `followers-restart-package-native.log`.
- Death fallback/return, another housecarl and a paid hireling: `followers-life-native2.log`.
- Animal recruitment by guest activation: `followers-activation-native.log`.
- Placed Lydia save/load and guest-led real load door: `followers-save-travel.log` and
  `followers-fresh-save-door.log`. The placed save assertion passed; the latter run's final audit reported
  a native alias/load lock inversion during fixture dismissal. The identical pair was reproduced on unchanged
  dev (`followers-baseline-alias.log`), reviewed and added to the existing known load patterns.

A debug `coc` from doorless QASmoke to the inn left a following NPC behind; the real inn load door passed.
This implementation does not add an NPC teleport tether. Quest teleports, jail, fast travel, waiting through
72 game hours, directed favors, combat during fallback and a native three/four-player run still need explicit
acceptance. Temporary quest companions and custom follower frameworks keep their own aliases/packages; they
are outside the shared vanilla slots. The fake's three-player check does not prove those native engine paths.

An outdoor run also crashed in native horse/Noble follow AI before loading a save
(`followers-first-crash.log`, 39136, horse FF000AE0). That actor was not the recruited follower and the new target
hook was absent from the crash stack. Cause remains unconfirmed; do not count the interrupted cases as passes.
