#pragma once

#include <network/ENetInterface.h>
#include <Messages/JoinMessages.h>
#include <NoCopyMove.h>

#include <map>
#include <memory>
#include <vector>

struct HostGame;
struct HostNetwork;

/**
 * @brief The host's save for games joining from the main menu (docs/design/join-flow.md, "As built"): the host's game
 * saves once for the games waiting, then each gets that save in chunks, a few each update. A game whose save the host's
 * game could not make within kSaveSeconds is refused (transfer_failed).
 */
struct HostSaveTransfer
{
    using ConnectionId_t = Net::ConnectionId_t;

    static constexpr size_t kChunksPerUpdate = 8;
    static constexpr uint64_t kSaveSeconds = 60;

    HostSaveTransfer(HostNetwork& aNetwork, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostSaveTransfer);

    // A game passed the gate from the main menu: it gets the next save.
    void Want(ConnectionId_t aConnection) noexcept;
    // Every frame (the session's tick, in milliseconds).
    void Update(uint64_t aTick) noexcept;
    // A party wipe's save to acGuests and to every game still joining, each told first (NotifyPartyReload).
    void Checkpoint(std::vector<NotifySaveChunk> aChunks, const std::vector<ConnectionId_t>& acGuests) noexcept;
    [[nodiscard]] bool Empty() const noexcept { return m_receivers.empty(); }
    void OnDisconnected(ConnectionId_t aConnection) noexcept;
    [[nodiscard]] bool IsSending(ConnectionId_t aConnection) const noexcept { return m_receivers.contains(aConnection); }

private:
    using Chunks = std::vector<NotifySaveChunk>;
    struct Receiver
    {
        std::shared_ptr<const Chunks> Save; // null: waiting for the next save
        size_t Next{};
        uint64_t Since{}; // the tick it started waiting
    };

    HostNetwork& m_network;
    const HostGame& m_game;
    std::map<ConnectionId_t, Receiver> m_receivers;
};
