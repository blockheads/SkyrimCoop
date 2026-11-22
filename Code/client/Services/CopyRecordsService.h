#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <Services/CopyRecords.h>

#include <NoCopyMove.h>

struct World;
struct TransportService;
struct DisconnectedEvent;
struct NotifyCharacterSpawn;
struct NotifyRemoveCharacter;

/**
 * @brief The copy records' messages (docs/design/one-simulator.md, piece 1): an owner's declared part goes to the
 * host, the other games' parts and each spawn's record come in, and an asked part goes out again.
 */
struct CopyRecordsService
{
    CopyRecordsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, CopyRecords& aRecords) noexcept;
    ~CopyRecordsService() noexcept;

    NO_COPY_MOVE(CopyRecordsService);

    // A copy's part looks wrong: its owner sends it again (R3).
    void Ask(uint32_t aId, RecordKind aKind) const noexcept;

private:
    void OnNotifyRecord(const NotifyRecord& acMessage) noexcept;
    void OnNotifyRecordAsk(const NotifyRecordAsk& acMessage) noexcept;
    void OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;

    World& m_world;
    TransportService& m_transport;
    CopyRecords& m_records;
    std::vector<entt::scoped_connection> m_connections;
};
