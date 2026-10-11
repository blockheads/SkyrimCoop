# Copy contract

A **copy** is the stand-in a game shows for an actor another game owns. That covers a remote
player's character and the client's replica of a host NPC (`ActorExtension::IsRemote()`).

Skyrim has exactly one player: `g_thePlayer` is read in 2843 places across 1286 engine functions.
So remote players can only be NPC-bodied actors. The engine then treats copies like any NPC and
runs code paths vanilla never runs with a "player-like" actor. Each system we sync adds more of
those meetings.

**The rule:** a copy is a puppet with an NPC body. The owner's game decides everything it does.
The copy shows the result and takes part in what *other* actors do to it, nothing more.

## What a copy keeps and what it gives up

| System | Copy takes part? | How it is enforced / where |
|---|---|---|
| Body, 3D, animation graph, collision | yes | the movement and animation sync drive it |
| Faction membership (others' reactions to it) | yes | copies keep their factions; ally anger is our registry (Actor.cpp) |
| Being targeted, hit, damaged, killed | yes, by the owner | hits are requested from the owner (ActorValueService, MagicTarget hook) |
| Per-frame AI update | no | `HookActorProcess` (Actor.cpp) skips remote actors |
| AI packages (picked or engine-created) | no | `HookCheckForNewPackage` and `HookPutCreatedPackage` (References.cpp) |
| Package sync from the owner | no | `AI_SYNC` stays 0 (References.cpp) |
| Applying spell effects locally | no | the owner applies them (MagicTarget.cpp) |
| Combat decisions, dialogue, reservations | no (audit when touched) | to check when syncing combat or dialogue |
| Crime, followers, quests | undecided | decide when the system is synced |

## When syncing a new system

1. Decide which row it falls under, before writing sync code.
2. If copies must not take part, block it at the engine's single entry point for that system,
   in one hook, and add the row here.
3. Run the scenarios with the lock checker on (`coop_test.py locks`). A new pattern with our
   code on the stack fails the scenario.

## Why this exists

On 2026-10-10 the host deadlocked when it disabled an NPC that was fighting the client's copy.
1. The engine's Disable cleanup (41331) holds the player's read lock while it ends packages
   aimed at the disabled NPC.
2. The copy had a package the engine created for it ("movement blocked", flee) that reserves
   the player.
3. Resuming that package asked for the player's write lock on the same thread.
The lock checker named it on the first hang. The fix was this contract's package rows.
