# Glossary

The words code, comments and docs use (decided 2026-10-20). Skyrim Together was an MMO, a server and its
clients; some of its words are still in the code until the migration removes them (last section).

## The session

| Word | Means | In code |
|---|---|---|
| **host** | The game everyone joins. Its Skyrim is the only authority: it owns every NPC and the world, and decides quests | `HostService`, `HostBridge`, `Services/Host/` |
| **guest** | A game that joined the host. It owns only its own player's character | (was "client" or "peer") |
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

A host NPC is local on the host and a copy on each guest. A guest player's character is local on their
game and a copy (an other player) on the host and the other guests.

## Messages

| Word | Means | In code |
|---|---|---|
| **request** | A message to the host | `Request*`, `*Request`, `ClientMessage` |
| **notify** | A message from the host | `Notify*`, `ServerMessage` |
| **server id** | The id the host gives each synced actor, the same on every game | `ServerId` |

## Old words still in code

| In code | Now means | Goes |
|---|---|---|
| `RemoteComponent` | a copy | renamed to `CopyComponent` next |
| `ClientMessage` / `ServerMessage` | request / notify | with migration phase 5 |
| `ServerId` | server id (above) | with migration phase 5 |
| `Code/server/`, `GameServer` | the upstream server, still embedded in the host | deleted in migration phase 5 |
| `Code/client/`, `TiltedPhoques::Client`, "client" in build targets | the plugin and its transport, on the host and guests alike | stays; never means a guest |
| "peer", "client", "remote player", "member" in older docs | guest, guest, other player, player | as docs are touched |
