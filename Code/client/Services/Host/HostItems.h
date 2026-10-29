#pragma once

#include <network/ENetInterface.h>

#include <Messages/DroppedItems.h>

#include <map>
#include <utility>
#include <vector>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostGame;
struct HostNetwork;
struct HostPlayers;
struct HostCharacters;
struct HostPurse;
struct ContainerTransferRequest;

/**
 * @brief Dropped items (docs/design/dropped-items.md): a loose item exists once, in the host's world. A guest's drop
 * is made there (its game's own reference becomes the twin), and a guest's pickup is the host's to grant: the first
 * player to ask gets it, and it is gone for everyone.
 */
struct HostItems
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Drops whose cell the host's game has not loaded wait; past this many the oldest is forgotten.
    static constexpr size_t kMaxPending = 64;

    HostItems(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostPurse& acPurse, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostItems);

    // Each update: the waiting drops the host's game can make now.
    void Update() noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    void OnContainerTransfer(const ContainerTransferRequest&, ConnectionId_t) noexcept;
    void OnDrop(const DropItemRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnPickUp(const PickUpRequest& acRequest, ConnectionId_t aSender) noexcept;
    // False while its cell is not loaded here (true once made, or for one the host's game cannot make).
    bool Make(ConnectionId_t aPlayer, const DropItemRequest& acRequest) noexcept;
    void Grant(ConnectionId_t aPlayer, const GameId& acObject, uint32_t aTag, const Inventory::Entry& acItem) const noexcept;

    struct Pending
    {
        ConnectionId_t Player;
        DropItemRequest Request;
    };

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostPurse& m_purse;
    const HostGame& m_game;
    std::vector<Pending> m_pending;                                  // oldest first
    std::map<std::pair<ConnectionId_t, uint32_t>, GameId> m_made;    // a guest's drop made here, by its tag
};
