#pragma once

#include "UiModels.h"
#include <chrono>
#include <span>

namespace Ui
{
// Engine-free visibility policy. Skyrim supplies whether its HUD is currently allowed to show.
class PartyActivity
{
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto ShowDuration = std::chrono::seconds(8);
    static constexpr auto FadeDuration = std::chrono::milliseconds(400);

    void Update(std::span<const Member> aMembers, bool aConnected, bool aHudVisible, Clock::time_point aNow);
    [[nodiscard]] float Opacity() const
    {
        return m_opacity;
    }

  private:
    Rml::Vector<Member> m_previous;
    Clock::time_point m_lastActivity{};
    float m_opacity = 0.f;
    bool m_active = false;
    bool m_pending = false;
};
} // namespace Ui
