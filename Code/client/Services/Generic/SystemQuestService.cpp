#include <Services/SystemQuestService.h>
#include <Services/TransportService.h>

#include <Events/SystemQuestLearnedEvent.h>
#include <FormIds.h>
#include <Messages/Relayed.h>
#include <Papyrus/SystemQuests.h>
#include <World.h>

SystemQuestService::SystemQuestService(World&, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_transport(aTransport)
{
    Listen<SystemQuestLearnedEvent, &SystemQuestService::OnLearned>(m_connections, aDispatcher, this);
    Listen<NotifySystemQuests, &SystemQuestService::OnNotifySystemQuests>(m_connections, aDispatcher, this);
}

void SystemQuestService::OnLearned(const SystemQuestLearnedEvent& acEvent) noexcept
{
    const auto cId = ToSession(acEvent.Quest);
    if (!cId || !m_transport.IsConnected())
        return;
    SystemQuestsRequest request;
    request.Quests = {*cId};
    m_transport.Send(request);
}

void SystemQuestService::OnNotifySystemQuests(const NotifySystemQuests& acMessage) noexcept
{
    for (const GameId& cId : acMessage.Quests)
        if (const uint32_t cQuest = ToGame(cId))
            SystemQuests::Learn(cQuest, false);
}
