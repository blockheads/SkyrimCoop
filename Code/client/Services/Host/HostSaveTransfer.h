#pragma once

#include <network/ENetInterface.h>
#include <Messages/JoinMessages.h>
#include <NoCopyMove.h>
#include <Services/SaveTransfer.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct HostGame;
struct HostNetwork;

/**
 * @brief The host's saves on their way to the guests (docs/design/join-flow.md, "Save sync"): the save a joining game
 * loads, the save a host load or party wipe takes everyone back to, and every save the host makes while playing,
 * sent ahead so a later load of it needs no transfer. Each goes as its difference from a save that guest holds.
 */
struct HostSaveTransfer
{
    using ConnectionId_t = Net::ConnectionId_t;

    // A join's or a load's: the game waits on it.
    static constexpr size_t kBytesPerUpdate = 256 * 1024;
    // A save sent ahead goes behind the game's own messages: a piece an update, while little is queued.
    static constexpr size_t kAheadBytesPerUpdate = NotifySaveChunk::kMaxData;
    static constexpr uint32_t kAheadQueue = 32;
    static constexpr uint32_t kQueue = 512;
    static constexpr uint64_t kSaveSeconds = 60;

    HostSaveTransfer(HostNetwork& aNetwork, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostSaveTransfer);

    // The saves a game's store holds: on its join request, and whenever it says so (SaveHeldRequest).
    void SetHeld(ConnectionId_t aConnection, std::vector<std::string> aSaves) noexcept;
    void OnSaveHeld(const SaveHeldRequest& acRequest, ConnectionId_t aSender) noexcept { SetHeld(aSender, acRequest.Saves); }
    // A game passed the gate from the main menu: it gets the next save.
    void Want(ConnectionId_t aConnection) noexcept;
    // The host's game made a save: ahead to each of acGuests that does not hold it, replacing an older one on its way.
    void Offer(const std::string& acSave, const std::vector<ConnectionId_t>& acGuests) noexcept;
    // A host load or party wipe: acGuests and every game still joining are told (NotifyPartyReload), and get the
    // save unless they hold it.
    void Checkpoint(const std::string& acSave, const std::vector<ConnectionId_t>& acGuests) noexcept;
    // Every frame (the session's tick, in milliseconds).
    void Update(uint64_t aTick) noexcept;
    // No game waits on a save (those sent ahead do not count).
    [[nodiscard]] bool Empty() const noexcept;
    void OnDisconnected(ConnectionId_t aConnection) noexcept;
    [[nodiscard]] bool IsSending(ConnectionId_t aConnection) const noexcept;

private:
    struct Receiver
    {
        std::string Save;                                 // empty: waiting for the host's game to save
        bool Ahead{};
        std::shared_ptr<const SaveTransfer::Stream> Data; // null: being encoded
        size_t Next{};                                    // the next piece's offset
        uint64_t Since{};                                 // the tick it started waiting
    };

    [[nodiscard]] bool Holds(ConnectionId_t aConnection, const std::string& acSave) const noexcept;
    void Refuse(ConnectionId_t aConnection) noexcept;

    HostNetwork& m_network;
    const HostGame& m_game;
    std::map<ConnectionId_t, Receiver> m_receivers;
    std::map<ConnectionId_t, std::vector<std::string>> m_held;
    std::string m_checkpoint;        // the save a host load or wipe goes back to
    std::set<ConnectionId_t> m_told; // told to load it from their store, nothing sent
};
