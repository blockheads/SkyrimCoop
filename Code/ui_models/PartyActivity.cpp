#include "PartyActivity.h"

#include <algorithm>
#include <cmath>

namespace Ui
{
void PartyActivity::Update(std::span<const Member> aMembers, bool aConnected, bool aHudVisible, Clock::time_point aNow)
{
    if (!aConnected)
    {
        m_previous.clear();
        m_opacity = 0.f;
        m_active = m_pending = false;
        return;
    }

    // Someone joining or leaving shows the panel; the chat says who (SessionService's notices).
    bool changed = false;
    Rml::Vector<Member> next(aMembers.begin(), aMembers.end());
    for (auto& member : next)
    {
        const auto old = std::find_if(m_previous.begin(), m_previous.end(),
                                      [&](const auto& previous) { return previous.Name == member.Name; });
        if (old == m_previous.end())
        {
            changed = true;
            continue;
        }
        const bool healthStateChanged =
            (old->Health < 0.f) != (member.Health < 0.f) || (old->Health == 0.f) != (member.Health == 0.f);
        if (healthStateChanged || std::abs(old->Health - member.Health) >= 0.001f)
            changed = true;
        else
            member.Health = old->Health; // Accumulate small changes against the last significant sample.
    }
    for (const auto& member : m_previous)
        if (std::none_of(next.begin(), next.end(), [&](const auto& current) { return current.Name == member.Name; }))
            changed = true;
    m_previous = std::move(next);
    if (changed)
        m_pending = true;

    if (!aHudVisible)
    {
        // Activity during a menu/cinematic, or an interrupted notification, gets a full display
        // window when the native HUD comes back. An already quiet party stays hidden.
        m_pending = m_pending || (m_active && aNow < m_lastActivity + ShowDuration + FadeDuration);
        m_opacity = 0.f;
        return;
    }
    if (m_pending)
    {
        m_lastActivity = aNow;
        m_active = true;
        m_pending = false;
    }
    if (!m_active)
    {
        m_opacity = 0.f;
        return;
    }
    const auto fadeElapsed = aNow - m_lastActivity - ShowDuration;
    m_opacity = std::clamp(1.f - std::chrono::duration<float>(fadeElapsed).count() /
                                     std::chrono::duration<float>(FadeDuration).count(),
                           0.f, 1.f);
    if (m_opacity == 0.f)
        m_active = false;
}
} // namespace Ui
