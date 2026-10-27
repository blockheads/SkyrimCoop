# NPC followers

Shared-slot/member-anchor design approved by the user, 2026-10-27. Feature 7 has **no implementation**.
Implementation is deferred while the party travel changes are reviewed on dev.
NPC simulation and the world remain the host's; a follower's trading partner is the player who picked the line
([party-model.md](party-model.md)). A player following the travel tether is a separate concept.

## What vanilla does

Measured on AE 1.6.1170 with two games, using the guest's real dialogue menu and its recruitment choice on a
host-created Lydia. Evidence: `build/follower-measurement.log` and `build/follower-records.log` in party-travel.

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

Only Lydia's guest recruitment and native follow target were measured live. Housecarls, hired followers and
dogs still require their own live acceptance cases before follower implementation can be called complete.

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
  picker; trading alone does not. The host orders competing commands once.
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

## Implementation follow-up

The user approved recruitment and Follow choosing the physical pathing target, shared access to other commands,
and automatic fallback when the anchor dies or disconnects. Vanilla host targeting remains an acceptable
fallback if native retargeting proves too costly. No follower service, package hook, new follower message,
recruitment rule or trading implementation has been added.

When implementation resumes, first test binding and picker inventory rules in fast tests, then run native cases for Lydia,
a housecarl, a paid hireling and a dog: host/guest recruitment, slot contention, switching anchors, wait/timeouts,
favors, trading, dismissal, combat/death, doors/fast travel/jail, three players, host save/load, and reconnect
with a replacement player copy. Verify NPC inventories and the number of NPCs after every reconnect.

Package targeting and picker inventory routing may require coordination with the script-layer agent. Those
changes, cutscenes, message natives and story-event hooks remain outside this worktree's implementation scope.
