#include <Bridges/IPlayerBridge.h>
#include <Events/DisconnectedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/FollowerMessages.h>
#include <Services/DialogueService.h>
#include <Services/FollowerService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>
#include <utility>

FollowerService::FollowerService(World& aWorld, entt::dispatcher& aDispatcher, IPlayerBridge& aPlayer) noexcept
    : m_world(aWorld), m_player(aPlayer)
{
    Listen<NotifyFollowerTrade, &FollowerService::OnTrade>(m_connections, aDispatcher, this);
    Listen<MenuEvent, &FollowerService::OnMenu>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &FollowerService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &FollowerService::OnDisconnected>(m_connections, aDispatcher, this);
}

void FollowerService::OnTrade(const NotifyFollowerTrade& acMessage) noexcept
{
    if (m_trading)
    {
        FollowerTradeClosedRequest close;
        close.NpcId = acMessage.NpcId;
        m_world.GetTransport().Send(close);
        return;
    }
    m_trading = acMessage.NpcId;
    m_opened = false;
    m_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    m_pendingNpc = Utils::GetFormIdBySessionId(acMessage.NpcId);
    if (!m_pendingNpc)
    {
        Release();
        return;
    }
    auto& dialogue = m_world.ctx().at<DialogueService>().Bridge();
    if (dialogue.IsMenuOpen())
        dialogue.CloseMenu();
    else
        OpenPending();
}

void FollowerService::OnMenu(const MenuEvent& acEvent) noexcept
{
    if (!m_trading || acEvent.Menu != "ContainerMenu")
        return;
    if (acEvent.Opening)
        m_opened = true;
    else
        Release();
}
void FollowerService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_pendingNpc && !m_world.ctx().at<DialogueService>().Bridge().IsMenuOpen())
        OpenPending();
    if (m_trading && !m_opened && std::chrono::steady_clock::now() >= m_until)
        Release();
}
void FollowerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_trading = m_pendingNpc = 0;
}
void FollowerService::Release() noexcept
{
    if (!m_trading)
        return;
    m_pendingNpc = 0;
    FollowerTradeClosedRequest close;
    close.NpcId = std::exchange(m_trading, 0u);
    m_world.GetTransport().Send(close);
}

void FollowerService::OpenPending() noexcept
{
    const uint32_t cNpc = std::exchange(m_pendingNpc, 0u);
    m_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    if (!m_player.OpenFollowerInventory(cNpc))
        Release();
}
