# SkyrimCoop P2P Conversion Plan

This document outlines the step-by-step plan to convert the current client-server MMO architecture to a **Listen Server** (host-client) P2P model.

## Terminology

**Listen Server / Host-Client Model:**
- One player runs both client and server simultaneously
- Host player is the authoritative source of truth
- Other players connect as clients to the host
- Same pattern used by: Valheim, Don't Starve Together, Terraria, Minecraft, Left 4 Dead

**Why This Approach?**
- ✅ Simplicity: Clear authority (host's game state)
- ✅ Proven: Used by most successful co-op games
- ✅ Performance: Host has zero latency
- ✅ Control: Easy session management
- ✅ Security: Host validates client actions
- ❌ Limitation: No host migration (acceptable for co-op sessions)

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Phase 1: Embed Server in Client](#phase-1-embed-server-in-client)
3. [Phase 2: Simplify Ownership Model](#phase-2-simplify-ownership-model)
4. [Phase 3: Simplify Broadcasting](#phase-3-simplify-broadcasting)
5. [Phase 4: Quest Synchronization](#phase-4-quest-synchronization)
6. [Phase 5: Connection & Discovery](#phase-5-connection--discovery)
7. [Phase 6: UI Updates](#phase-6-ui-updates)
8. [Testing Strategy](#testing-strategy)
9. [Rollback Plan](#rollback-plan)

---

## Architecture Overview

### Current Architecture (Dedicated Server)

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│  Client A   │         │  Client B   │         │  Client C   │
│  (Player)   │         │  (Player)   │         │  (Player)   │
└──────┬──────┘         └──────┬──────┘         └──────┬──────┘
       │                       │                        │
       │      UDP              │         UDP            │
       └───────────┬───────────┴────────────┬───────────┘
                   │                        │
                   ▼                        ▼
            ┌──────────────────────────────────┐
            │      Dedicated GameServer        │
            │      (Standalone Process)        │
            │  - Authoritative World State     │
            │  - Validates all actions         │
            │  - Broadcasts to all clients     │
            └──────────────────────────────────┘
```

### Target Architecture (Listen Server)

```
┌────────────────────────────────────┐       ┌─────────────┐       ┌─────────────┐
│         Host Player (Client A)     │       │  Client B   │       │  Client C   │
│  ┌──────────────────────────────┐  │       │  (Player)   │       │  (Player)   │
│  │  Client Side                 │  │       └──────┬──────┘       └──────┬──────┘
│  │  - Plays Skyrim              │  │              │                      │
│  │  - Renders world             │  │              │                      │
│  │  - Handles input             │  │              │  UDP                 │  UDP
│  └───────┬──────────────────────┘  │              │                      │
│          │ localhost (IPC)         │              │                      │
│          ▼                          │              │                      │
│  ┌──────────────────────────────┐  │              │                      │
│  │  Embedded GameServer         │  │              │                      │
│  │  - Authoritative World State │◄─┼──────────────┴──────────────────────┘
│  │  - Validates actions         │  │
│  │  - Broadcasts to clients     │  │
│  └──────────────────────────────┘  │
└────────────────────────────────────┘

Key: Host runs BOTH client and server in same process
     Clients connect to host's IP:Port via UDP
```

---

## Phase 1: Embed Server in Client

**Goal:** Run GameServer instance inside the client process when hosting a session.

### 1.1 Create HostService

**File:** `Code/client/Services/HostService.h`

```cpp
#pragma once

#include <memory>
#include <GameServer.h>

struct World;
struct UpdateEvent;

/**
 * @brief Manages embedded GameServer when player is hosting a session.
 */
struct HostService
{
    HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~HostService() noexcept;

    TP_NOCOPYMOVE(HostService);

    // Host session management
    bool StartHosting(uint16_t aPort = 10578) noexcept;
    void StopHosting() noexcept;
    bool IsHosting() const noexcept { return m_isHosting; }

    // Server access
    GameServer* GetServer() const noexcept { return m_pGameServer.get(); }

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    World& m_world;
    entt::dispatcher& m_dispatcher;

    bool m_isHosting{false};
    std::unique_ptr<GameServer> m_pGameServer;
    std::unique_ptr<Console::ConsoleRegistry> m_pConsole;

    entt::scoped_connection m_updateConnection;
};
```

**File:** `Code/client/Services/HostService.cpp`

```cpp
#include "HostService.h"
#include <World.h>
#include <Events/UpdateEvent.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>()
        .connect<&HostService::OnUpdate>(this);
}

HostService::~HostService() noexcept
{
    StopHosting();
}

bool HostService::StartHosting(uint16_t aPort) noexcept
{
    if (m_isHosting)
        return true;

    try {
        // Create console registry for server
        m_pConsole = std::make_unique<Console::ConsoleRegistry>();

        // Create embedded GameServer
        m_pGameServer = std::make_unique<GameServer>(*m_pConsole);

        // Initialize server
        m_pGameServer->Host(aPort, 8); // Port, Max Players

        m_isHosting = true;

        spdlog::info("Started hosting session on port {}", aPort);
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to start hosting: {}", e.what());
        m_pGameServer.reset();
        m_pConsole.reset();
        return false;
    }
}

void HostService::StopHosting() noexcept
{
    if (!m_isHosting)
        return;

    if (m_pGameServer) {
        m_pGameServer->Close();
        m_pGameServer.reset();
    }

    m_pConsole.reset();
    m_isHosting = false;

    spdlog::info("Stopped hosting session");
}

void HostService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_isHosting && m_pGameServer) {
        // Update embedded server every frame
        m_pGameServer->Update();
    }
}
```

### 1.2 Modify TransportService for Localhost Connection

**File:** `Code/client/Services/TransportService.h`

Add methods:

```cpp
struct TransportService : Client
{
    // ... existing methods ...

    // Connect to embedded server (host mode)
    bool ConnectToLocalhost(uint16_t aPort = 10578) noexcept;

    // Connect to remote host (client mode)
    bool ConnectToHost(const String& aAddress, uint16_t aPort = 10578) noexcept;

    bool IsHost() const noexcept { return m_isHost; }

private:
    bool m_isHost{false};
};
```

**File:** `Code/client/Services/TransportService.cpp`

```cpp
bool TransportService::ConnectToLocalhost(uint16_t aPort) noexcept
{
    m_isHost = true;
    return Connect("127.0.0.1", aPort);
}

bool TransportService::ConnectToHost(const String& aAddress, uint16_t aPort) noexcept
{
    m_isHost = false;
    return Connect(aAddress, aPort);
}
```

### 1.3 Integrate HostService into TiltedOnlineApp

**File:** `Code/client/TiltedOnlineApp.h`

```cpp
class TiltedOnlineApp
{
    // ... existing members ...

    std::unique_ptr<HostService> m_pHostService;

public:
    void StartHostSession(uint16_t aPort = 10578);
    void JoinHostSession(const String& aAddress, uint16_t aPort = 10578);
    void LeaveSession();

    bool IsHosting() const;
};
```

**File:** `Code/client/TiltedOnlineApp.cpp`

```cpp
void TiltedOnlineApp::BeginMain()
{
    // ... existing initialization ...

    // Create host service
    m_pHostService = std::make_unique<HostService>(m_world, m_dispatcher);

    // ... rest of initialization ...
}

void TiltedOnlineApp::StartHostSession(uint16_t aPort)
{
    // Start embedded server
    if (m_pHostService->StartHosting(aPort))
    {
        // Connect client to localhost
        m_pTransportService->ConnectToLocalhost(aPort);
    }
}

void TiltedOnlineApp::JoinHostSession(const String& aAddress, uint16_t aPort)
{
    // Connect as client to remote host
    m_pTransportService->ConnectToHost(aAddress, aPort);
}

void TiltedOnlineApp::LeaveSession()
{
    m_pTransportService->Close();

    if (m_pHostService->IsHosting())
    {
        m_pHostService->StopHosting();
    }
}

bool TiltedOnlineApp::IsHosting() const
{
    return m_pHostService && m_pHostService->IsHosting();
}
```

### 1.4 Testing Phase 1

**Test Cases:**
1. Start hosting → Embedded server runs → Client connects to localhost
2. Stop hosting → Server shuts down → Client disconnects
3. Join as client → Connect to remote IP → No embedded server starts
4. Host spawns actor → Server processes → Broadcasts to self and clients

**Success Criteria:**
- Host player can start/stop hosting
- Host client connects to embedded server via 127.0.0.1
- Remote clients can connect to host's IP
- All existing gameplay works in host mode

---

## Phase 2: Simplify Ownership Model

**Goal:** Host always owns NPCs and world actors. Clients only own their player character.

### 2.1 Current Ownership Logic (To Remove)

**Problems:**
- `OwnerComponent` tracks which player owns each entity
- Ownership can transfer between players (cell handoff)
- Complex logic to determine ownership

**Files to Modify:**
- `Code/server/Components/OwnerComponent.h` (keep but simplify)
- `Code/server/Services/CharacterService.cpp` (ownership assignment)
- `Code/encoding/Messages/RequestOwnershipTransfer.h` (deprecate)
- `Code/encoding/Messages/NotifyOwnershipTransfer.h` (deprecate)

### 2.2 New Ownership Rules

```cpp
// Simplified ownership determination
enum class ActorOwnership
{
    Host,    // Host owns all NPCs, world actors, summons
    Client   // Client only owns their player character
};

ActorOwnership DetermineOwnership(entt::entity aEntity, ConnectionId_t aConnectionId)
{
    // Is this the player's own character?
    if (IsPlayerCharacter(aEntity, aConnectionId))
        return ActorOwnership::Client;

    // Everything else is owned by host
    return ActorOwnership::Host;
}
```

### 2.3 Modify CharacterService (Server)

**File:** `Code/server/Services/CharacterService.cpp`

```cpp
void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const
{
    auto& message = acMessage.GetMessage();
    Player* pPlayer = acMessage.GetSender();

    // Create entity
    entt::entity entity = m_world.create();
    ApplyActorData(entity, message.CurrentActorData);

    // Assign server ID
    uint32_t serverId = m_world.GetNextServerId();
    m_world.emplace<ServerIdComponent>(entity, serverId);

    // SIMPLIFIED: Determine ownership
    bool isOwner = false;

    // Client owns their own player character
    if (message.FormId == pPlayer->GetCharacterFormId())
    {
        isOwner = true;
        m_world.emplace<OwnerComponent>(entity, pPlayer->GetConnectionId());
    }
    else
    {
        // Host owns all NPCs/world actors
        auto* pHostPlayer = GetHostPlayer(); // New helper function
        if (pHostPlayer)
        {
            m_world.emplace<OwnerComponent>(entity, pHostPlayer->GetConnectionId());
        }

        // Client requesting this gets Owner=false
        isOwner = false;
    }

    // Send response
    AssignCharacterResponse response;
    response.Cookie = message.Cookie;
    response.ServerId = serverId;
    response.Owner = isOwner;
    pPlayer->Send(response);

    // Broadcast spawn
    BroadcastActorData(pPlayer, entity, message.CurrentActorData);
}
```

### 2.4 Add Host Player Tracking

**File:** `Code/server/GameServer.h`

```cpp
struct GameServer final : Server
{
    // ... existing methods ...

    Player* GetHostPlayer() const noexcept { return m_pHostPlayer; }
    void SetHostPlayer(Player* apPlayer) noexcept { m_pHostPlayer = apPlayer; }

private:
    Player* m_pHostPlayer{nullptr}; // First connected player is host
};
```

**File:** `Code/server/GameServer.cpp`

```cpp
void GameServer::HandleAuthenticationRequest(ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest)
{
    // ... existing validation ...

    // Create player
    Player* pPlayer = CreatePlayer(aConnectionId);

    // First player is the host
    if (!m_pHostPlayer)
    {
        m_pHostPlayer = pPlayer;
        pPlayer->SetIsHost(true);
        spdlog::info("Player {} is now the host", pPlayer->GetUsername());
    }

    // ... send response ...
}
```

### 2.5 Remove Ownership Transfer Messages

**Deprecate (keep for backwards compatibility but don't use):**
- `RequestOwnershipTransfer`
- `NotifyOwnershipTransfer`
- `RequestOwnershipClaim`

**Remove handlers:**
- `CharacterService::OnOwnershipTransferRequest`
- `CharacterService::OnOwnershipClaimRequest`

### 2.6 Testing Phase 2

**Test Cases:**
1. Host spawns NPC → Owner = Host
2. Client spawns NPC → Owner = Host (not client!)
3. Client spawns own character → Owner = Client
4. NPC ownership never transfers
5. Host can control all NPCs
6. Clients can only control their character

**Success Criteria:**
- No ownership transfer messages sent
- Host has authority over all world state
- Clients only control player character
- NPCs behave correctly for all players

---

## Phase 3: Simplify Broadcasting

**Goal:** Remove cell-based filtering. Broadcast to all connected clients.

### 3.1 Current Broadcasting Logic (To Simplify)

**Problems:**
- `SendToPlayersInRange()` filters by cell/distance
- Complex spatial partitioning
- Cell-based culling logic

**Trade-off:**
- Current: Lower bandwidth, complex code
- P2P: Higher bandwidth, simple code
- Acceptable for 2-8 players in co-op

### 3.2 Modify GameServer Broadcasting

**File:** `Code/server/GameServer.cpp`

```cpp
// OLD (complex):
bool GameServer::SendToPlayersInRange(const ServerMessage& acMessage, const entt::entity acOrigin, const Player* apExcludeSender) const
{
    auto originPos = m_world.get<CharacterComponent>(acOrigin).Position;

    auto players = m_world.view<PlayerComponent, CharacterComponent>();
    for (auto player : players) {
        auto playerPos = m_world.get<CharacterComponent>(player).Position;
        if (Distance(originPos, playerPos) < kBroadcastRange) {
            // Send to player in range
        }
    }
}

// NEW (simple):
void GameServer::SendToAllClients(const ServerMessage& acMessage, const Player* apExcludeSender) const
{
    for (auto& [connectionId, pPlayer] : m_players)
    {
        if (pPlayer != apExcludeSender)
        {
            pPlayer->Send(acMessage);
        }
    }
}
```

### 3.3 Replace Range Checks

**Find and replace pattern:**

```cpp
// OLD:
SendToPlayersInRange(message, entity, pSender);

// NEW:
SendToAllClients(message, pSender);
```

**Files to update:**
- `Code/server/Services/CharacterService.cpp` (movement broadcasts)
- `Code/server/Services/InventoryService.cpp` (equipment broadcasts)
- `Code/server/Services/MagicService.cpp` (spell broadcasts)
- `Code/server/Services/CombatService.cpp` (combat broadcasts)

### 3.4 Remove Cell Management Complexity

**Simplify:**
- Keep cell tracking for host's loaded cells
- Don't track which clients are in which cells
- Clients receive all updates, filter locally if needed

**Files to modify:**
- `Code/server/Services/MapService.cpp` (simplify cell tracking)
- `Code/server/Services/PlayerService.cpp` (remove cell enter/exit messages)

### 3.5 Testing Phase 3

**Test Cases:**
1. Host moves → All clients receive update (even if far away)
2. Client moves → Host + all clients receive update
3. 8 players in different cells → All receive all updates
4. Bandwidth monitoring → Should be acceptable for co-op

**Success Criteria:**
- All clients receive all game events
- No range-based filtering
- Gameplay feels smooth for 2-8 players
- Bandwidth < 1 Mbps for typical gameplay

---

## Phase 4: Quest Synchronization

**Goal:** Force all players to sync to host's quest progress.

### 4.1 Current Quest System (To Modify)

**Problems:**
- Quest sync is optional
- Each player can be on different quest stages
- Server stores per-player quest state

**Solution:**
- Enable quest sync by default
- Host's quest progress is authoritative
- Clients mirror host's quest state

### 4.2 Modify QuestService (Server)

**File:** `Code/server/Services/QuestService.cpp`

```cpp
void QuestService::OnQuestUpdateRequest(const PacketEvent<RequestQuestUpdate>& acMessage) const
{
    Player* pPlayer = acMessage.GetSender();
    Player* pHost = GameServer::Get()->GetHostPlayer();

    // Only accept quest updates from host
    if (pPlayer != pHost)
    {
        spdlog::warn("Ignoring quest update from non-host player {}", pPlayer->GetUsername());
        return;
    }

    // Apply host's quest update to all players
    auto& message = acMessage.GetMessage();

    // Update server's quest state
    UpdateQuestLog(message.QuestId, message.Stage);

    // Broadcast to all clients (including host for confirmation)
    NotifyQuestUpdate notification;
    notification.QuestId = message.QuestId;
    notification.Stage = message.Stage;
    notification.Objectives = message.Objectives;

    GameServer::Get()->SendToAllClients(notification);
}
```

### 4.3 Modify QuestService (Client)

**File:** `Code/client/Services/QuestService.cpp`

```cpp
void QuestService::OnNotifyQuestUpdate(const NotifyQuestUpdate& acMessage) noexcept
{
    // Apply quest update from host
    auto* pQuest = FindQuest(acMessage.QuestId);
    if (!pQuest)
        return;

    // Force set quest stage
    pQuest->SetCurrentStage(acMessage.Stage);

    // Update objectives
    for (auto& objective : acMessage.Objectives)
    {
        pQuest->SetObjectiveState(objective.Index, objective.State);
    }

    spdlog::info("Quest {} synced to stage {}", acMessage.QuestId, acMessage.Stage);
}
```

### 4.4 UI Notification for Clients

**Add to UI:**
- Display message: "Quest '{Quest Name}' updated by host"
- Show quest progress changes
- Notify if client diverges (resync to host)

### 4.5 Testing Phase 4

**Test Cases:**
1. Host completes quest objective → All clients update
2. Client tries to complete objective → Syncs back to host's state
3. Host starts new quest → All clients start same quest
4. Client is ahead of host → Client resyncs to host's progress

**Success Criteria:**
- All players always on same quest stages
- Host controls quest progression
- No quest desync issues
- Clear UI feedback for quest changes

---

## Phase 5: Connection & Discovery

**Goal:** Replace server list with platform lobby system (Steam/EOS).

### 5.1 Current Authentication (To Replace)

**Problems:**
- `AuthenticationRequest` has username/password
- Server validates credentials
- Dedicated server infrastructure

**Solution:**
- Use Steam/EOS friend invites
- No password needed
- Direct IP connection or platform lobby

### 5.2 Steam Integration (Option A)

**Using Steam Networking Sockets:**

**File:** `Code/client/Services/SteamLobbyService.h`

```cpp
#pragma once

#include <steam/steam_api.h>

struct SteamLobbyService
{
    // Create lobby for hosting
    bool CreateLobby(int maxPlayers = 8);

    // Join friend's lobby
    bool JoinLobby(CSteamID lobbyId);

    // Get lobby connection info
    String GetHostAddress();
    uint16_t GetHostPort();

    // Invite friend
    void InviteFriend(CSteamID friendId);
};
```

**Implementation:**
```cpp
bool SteamLobbyService::CreateLobby(int maxPlayers)
{
    if (!SteamAPI_Init())
        return false;

    // Create lobby
    SteamAPICall_t hCall = SteamMatchmaking()->CreateLobby(
        k_ELobbyTypeInvisible,  // Friends-only
        maxPlayers
    );

    // Store host IP/port in lobby metadata
    // ...

    return true;
}
```

### 5.3 Direct IP Connection (Option B - Simpler)

**For initial implementation:**

**UI Flow:**
1. Host clicks "Host Game" → Gets IP address → Shares with friends
2. Client clicks "Join Game" → Enters host's IP → Connects

**File:** `Code/client/Services/ConnectionService.h`

```cpp
struct ConnectionService
{
    // Get this machine's local IP
    String GetLocalIP();

    // Get public IP (via STUN server)
    String GetPublicIP();

    // Test connection to host
    bool TestConnection(const String& aAddress, uint16_t aPort);
};
```

### 5.4 Remove Server List Service

**Deprecate:**
- `Code/server/Services/ServerListService.h`
- `Code/client/Services/DiscoveryService.h`

**Replace with:**
- Steam lobby browser (long-term)
- Direct IP entry (short-term)
- LAN discovery (optional)

### 5.5 Simplify Authentication

**File:** `Code/encoding/Messages/AuthenticationRequest.h`

```cpp
struct AuthenticationRequest final : ClientMessage
{
    // BEFORE:
    String Username;
    String Password;
    Mods ModList;

    // AFTER (simplified):
    String Username;  // Steam/EOS username
    uint64_t UserId;  // Steam/EOS user ID
    Mods ModList;     // Still validate mod compatibility
    // No password needed (friend-based)
};
```

**File:** `Code/server/GameServer.cpp`

```cpp
void GameServer::HandleAuthenticationRequest(ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest)
{
    // REMOVED: Password validation
    // REMOVED: Server whitelist/blacklist

    // Simplified validation:
    // 1. Check mod compatibility
    if (!ValidateModList(acRequest->ModList))
    {
        SendAuthenticationFailure(aConnectionId, "Mod mismatch");
        return;
    }

    // 2. Check max players
    if (m_players.size() >= m_maxPlayers)
    {
        SendAuthenticationFailure(aConnectionId, "Server full");
        return;
    }

    // 3. Accept connection
    CreatePlayer(aConnectionId, acRequest->Username, acRequest->UserId);
}
```

### 5.6 Testing Phase 5

**Test Cases:**
1. Host creates lobby → Gets shareable IP/invite
2. Client joins via IP → Connects successfully
3. Mod mismatch → Connection rejected with clear error
4. Server full → Connection rejected
5. Steam friend invite → Joins via lobby

**Success Criteria:**
- No password prompts
- Easy connection via IP or lobby
- Clear error messages
- Mod validation works

---

## Phase 6: UI Updates

**Goal:** Update Angular UI for host/client modes.

### 6.1 Add Host/Join Screen

**File:** `Code/skyrim_ui/src/app/components/connection/connection.component.html`

```html
<div class="connection-screen">
  <!-- Host Section -->
  <div class="host-section">
    <h2>Host Game</h2>
    <button (click)="startHosting()">Start Hosting</button>

    <div *ngIf="isHosting">
      <p>Share this with friends:</p>
      <input readonly [value]="hostAddress" />
      <button (click)="copyAddress()">Copy IP</button>

      <p>Connected Players: {{ connectedPlayers.length }} / {{ maxPlayers }}</p>
      <ul>
        <li *ngFor="let player of connectedPlayers">
          {{ player.username }}
          <button (click)="kickPlayer(player.id)">Kick</button>
        </li>
      </ul>
    </div>
  </div>

  <!-- Join Section -->
  <div class="join-section">
    <h2>Join Game</h2>
    <input [(ngModel)]="hostIpAddress" placeholder="Enter host IP address" />
    <input [(ngModel)]="hostPort" placeholder="Port (default: 10578)" />
    <button (click)="joinHost()">Join</button>
  </div>
</div>
```

### 6.2 Update Connection Service

**File:** `Code/skyrim_ui/src/app/services/connection.service.ts`

```typescript
@Injectable()
export class ConnectionService {
  private isHosting$ = new BehaviorSubject<boolean>(false);
  private connectedPlayers$ = new BehaviorSubject<Player[]>([]);

  startHosting(port: number = 10578): void {
    this.clientService.sendMessage('host', { port });
  }

  joinHost(address: string, port: number = 10578): void {
    this.clientService.sendMessage('join', { address, port });
  }

  kickPlayer(playerId: number): void {
    this.clientService.sendMessage('kick', { playerId });
  }

  leaveSession(): void {
    this.clientService.sendMessage('leave', {});
  }
}
```

### 6.3 Add Host Indicator

**Show in UI:**
- Crown icon next to host's name in player list
- "You are hosting" banner when host
- "Connected to {Host Name}" when client

### 6.4 Remove Server Browser

**Delete/Hide:**
- Server browser component
- Server list display
- Server filters/search

**Replace with:**
- Simple host/join screen
- Recent connections list
- Favorite hosts list (by IP)

### 6.5 Testing Phase 6

**Test Cases:**
1. Host UI shows IP address and port
2. Host can see connected players
3. Host can kick players
4. Client UI shows host's name
5. Connection status updates in real-time
6. Error messages display clearly

**Success Criteria:**
- Intuitive host/join flow
- Clear visual feedback
- Easy to share connection info
- Host has management controls

---

## Testing Strategy

### Unit Testing

**New Components to Test:**
- `HostService::StartHosting()` / `StopHosting()`
- `TransportService::ConnectToLocalhost()`
- Simplified ownership logic
- Quest sync forcing

**Test Files:**
- `Code/tests/HostServiceTests.cpp`
- `Code/tests/OwnershipTests.cpp`
- `Code/tests/QuestSyncTests.cpp`

### Integration Testing

**Scenarios:**
1. **Solo Host:** Host starts, no clients connect → All features work
2. **Host + 1 Client:** Basic co-op → Movement, combat, quests sync
3. **Host + 4 Clients:** Stress test → Bandwidth, performance
4. **Client Disconnect:** Client drops → Host continues, others unaffected
5. **Host Disconnect:** Host drops → All clients disconnect (expected)
6. **Mod Mismatch:** Different mods → Connection rejected

### Performance Testing

**Metrics to Track:**
- Bandwidth usage per client (target: < 500 Kbps)
- Server CPU usage on host (target: < 50% single core)
- Client latency (target: < 100ms for LAN, < 200ms for internet)
- Memory usage (target: < 500 MB overhead)

### User Acceptance Testing

**Scenarios:**
1. Two friends play through a quest together
2. Group of 4 explores dungeon
3. PvP combat (if supported)
4. Trading items between players
5. Long session (2+ hours) → No memory leaks or crashes

---

## Rollback Plan

### Feature Flags

**Add configuration:**

```ini
[Multiplayer]
Mode=P2P          ; Options: P2P, Dedicated
EnableHostMode=1   ; 1 = Host-client, 0 = Client only
```

**Code:**
```cpp
bool IsPeerToPeerMode()
{
    return Config::Get().GetBool("Multiplayer", "Mode") == "P2P";
}

bool IsHostModeEnabled()
{
    return Config::Get().GetBool("Multiplayer", "EnableHostMode");
}
```

### Branch Strategy

**Git Branches:**
- `main` - Stable releases
- `dev` - Current development (original client-server)
- `p2p-conversion` - P2P work (this plan)
- `p2p-phase1` - Embedded server
- `p2p-phase2` - Ownership simplification
- `p2p-phase3` - Broadcasting
- `p2p-phase4` - Quest sync
- `p2p-phase5` - Connection
- `p2p-phase6` - UI

**Merge Strategy:**
- Each phase completes → Merge to `p2p-conversion`
- Full conversion tested → Merge to `dev`
- Release candidate → Merge to `main`

### Backwards Compatibility

**Keep for 1-2 versions:**
- Original `TransportService::Connect()` for dedicated servers
- Server list discovery (deprecated but functional)
- Old authentication messages

**Deprecation Timeline:**
- v1.0: P2P + Dedicated both supported
- v1.1: P2P recommended, Dedicated deprecated
- v2.0: P2P only, Dedicated removed

---

## Timeline Estimate

### Phase 1: Embed Server (2-3 weeks)
- Week 1: Create HostService, integrate into client
- Week 2: Localhost connection, testing
- Week 3: Bug fixes, polish

### Phase 2: Ownership (1-2 weeks)
- Week 1: Simplify ownership model, remove transfers
- Week 2: Testing, edge cases

### Phase 3: Broadcasting (1 week)
- Week 1: Remove range checks, simplify broadcasts, test bandwidth

### Phase 4: Quest Sync (1 week)
- Week 1: Force quest sync, UI feedback, testing

### Phase 5: Connection (2 weeks)
- Week 1: Direct IP connection, remove auth
- Week 2: Steam/EOS integration (optional)

### Phase 6: UI (1 week)
- Week 1: Host/join screen, player management

### Testing & Polish (2 weeks)
- Week 1: Integration testing, performance tuning
- Week 2: User testing, bug fixes

**Total: 10-12 weeks (2.5-3 months)**

---

## Success Metrics

### Technical Metrics
- ✅ Host can run embedded server
- ✅ Clients can connect to host's IP
- ✅ All gameplay features work in P2P mode
- ✅ Bandwidth < 1 Mbps per client
- ✅ Latency < 200ms for typical internet
- ✅ No crashes for 2+ hour sessions

### User Experience Metrics
- ✅ < 30 seconds to start hosting
- ✅ < 60 seconds for friend to join
- ✅ Clear error messages for connection issues
- ✅ Intuitive host/join UI
- ✅ Smooth gameplay for 2-8 players

### Quality Metrics
- ✅ 90%+ of original features working
- ✅ < 5% crash rate
- ✅ Positive user feedback
- ✅ Stable for public release

---

## Conclusion

This plan converts SkyrimCoop from a dedicated server MMO architecture to a **Listen Server (host-client)** P2P model:

1. **Phase 1:** Embed GameServer in client for hosting
2. **Phase 2:** Simplify ownership (host owns world)
3. **Phase 3:** Remove cell-based broadcasting complexity
4. **Phase 4:** Force quest sync to host
5. **Phase 5:** Replace authentication with platform invites
6. **Phase 6:** Update UI for host/join flow

**Result:** A co-op experience matching modern games (Valheim, Don't Starve Together) where friends can easily play together without dedicated servers.

**Next Steps:** Begin Phase 1 implementation after reviewing this plan.
