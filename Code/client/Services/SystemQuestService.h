#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct World;
struct TransportService;
struct SystemQuestLearnedEvent;

/**
 * @brief The quests one per player (docs/design/system-quests.md): one this game learned goes to the others through
 * the host (SystemQuestsRequest), and theirs are learned here (NotifySystemQuests). The save keeps the list (CoSave).
 */
struct SystemQuestService
{
    SystemQuestService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~SystemQuestService() noexcept = default;

    NO_COPY_MOVE(SystemQuestService);

protected:
    void OnLearned(const SystemQuestLearnedEvent& acEvent) noexcept;
    void OnNotifySystemQuests(const NotifySystemQuests& acMessage) noexcept;

private:
    TransportService& m_transport;

    std::vector<entt::scoped_connection> m_connections;
};
