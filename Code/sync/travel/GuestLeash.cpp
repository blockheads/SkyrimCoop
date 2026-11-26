#include <travel/GuestLeash.h>

#include <ports/IPlayerBridge.h>
#include <core/UpdateEvent.h>
#include <Messages/NotifyPartyTravel.h>
#include <core/World.h>

#include <algorithm>
#include <cmath>

GuestLeash::GuestLeash(World& aWorld, entt::dispatcher& aDispatcher, IPlayerBridge& aPlayer) noexcept
    : m_world(aWorld)
    , m_player(aPlayer)
{
    if (!m_world.ctx().contains<LeashCountdown>())
        m_world.ctx().emplace<LeashCountdown>();
    Listen<NotifyLeash, &GuestLeash::OnLeash>(m_connections, aDispatcher, this);
}

GuestLeash::~GuestLeash() noexcept
{
    Show(0);
}

void GuestLeash::Show(int aSeconds) noexcept
{
    m_shown = aSeconds;
    m_world.ctx().at<LeashCountdown>().Seconds = aSeconds;
}

void GuestLeash::OnLeash(const NotifyLeash& acMessage) noexcept
{
    if (acMessage.State == NotifyLeash::Kind::kOut)
    {
        m_left = acMessage.Seconds;
        Show(acMessage.Seconds);
        return;
    }
    const bool cCounting = m_left.has_value();
    m_left.reset();
    Show(0);
    if (cCounting && acMessage.State == NotifyLeash::Kind::kBack)
        m_player.ShowHudMessage("Back with the host");
}

void GuestLeash::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_left)
        return;
    *m_left -= acEvent.Delta;
    const int cSeconds = static_cast<int>(std::ceil(*m_left));
    // At 0 the host's trip takes over.
    if (cSeconds <= 0)
        m_left.reset();
    if (cSeconds != m_shown)
        Show(std::max(cSeconds, 0));
}
