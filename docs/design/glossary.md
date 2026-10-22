# Glossary

The words code, comments and docs use (decided 2026-10-20). Skyrim Together was an MMO, a server and its
clients; some of its words are still in the code until the migration removes them (last section).

## The session

| Word | Means | In code |
|---|---|---|
| **host** | The game everyone joins. Its Skyrim is the only authority: it owns every NPC and the world, and decides quests | `HostService`, `HostAuthority` (was `HostBridge`), `HostEngine`, `Services/Host/` |
| **guest** | A game that joined the host. It owns only its own player's character | `GuestService`, `GuestEngine`, `Services/Guest/` (was "client" or "peer") |
| **party** | Everyone in the session; party and session are one thing | `HostPlayers`, `SessionService` |
| **player** | A person in the party: the host player, a guest player | `PlayerComponent`, `PlayerId` |
| **this game's player** | The engine's player, form 0x14. Every game has exactly one, and "the player" in engine and script terms means only it | `PlayerCharacter::Get()` |

## Actors

| Word | Means | In code |
|---|---|---|
| **owner** | The game that simulates an actor: the host for NPCs, each player's own game for their character. Explaining an NPC, say "the host" | `HostCharacters::OwnerOf` |
| **local** | Owned by this game | `ActorExtension::IsLocal`, `LocalComponent` |
| **copy** | What every other game shows for an actor: an NPC body that runs no AI and does what its owner says (`copy-contract.md`) | `ActorExtension::IsCopy`, `IActorBridge::IsActorCopy` |
| **other player** | Another player's character, shown on this game as a copy: on the host, the guests' characters; on a guest, the host player's and the other guests' | `ActorExtension::IsOtherPlayer`, `IActorBridge::IsOtherPlayer` |
| **guest scripts** | On the host, the script rules for guests' actions: a guest's character is the player in scripts, and `Game.GetPlayer()` is the guest whose action started the script (`party-model.md`) | `Games/Skyrim/Papyrus/GuestScripts.cpp` |

Where code runs: `Services/Host/` and `Bridges/HostEngine` only while hosting; `Services/Guest/` and
`Bridges/GuestEngine` only while a guest (`GuestService` makes them on joining); everything else on every game.
Actors split by owner and copy instead: every game owns its own player and shows the others' as copies.

A host NPC is local on the host and a copy on each guest. A guest player's character is local on their
game and a copy (an other player) on the host and the other guests.

## Messages

| Word | Means | In code |
|---|---|---|
| **request** | A message to the host | `Request*`, `*Request`, `RequestMessage` (`RequestOpcode`) |
| **notify** | A message from the host (a reply to a request is a `*Response`) | `Notify*`, `*Response`, `NotifyMessage` (`NotifyOpcode`) |
| **session id** | The id the host gives each synced actor, the same on every game (from 1; 0 is none) | `SessionId` |

## Old words still in code

| In code | Now means | Goes |
|---|---|---|
| `RemoteComponent` | a copy (the ECS component; kept, alongside `LocalComponent`) | stays |
| `Code/client/`, `TiltedPhoques::Client`, "client" in build targets | the plugin and its transport, on the host and guests alike | stays; never means a guest |
| "peer", "client", "remote player", "member" in older docs | guest, guest, other player, player | as docs are touched |
