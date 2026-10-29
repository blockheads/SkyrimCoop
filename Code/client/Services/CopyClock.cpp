#include <Services/CopyClock.h>

#include <algorithm>

void CopyClock::OnSnapshot(uint64_t aOwnerTick, uint64_t aArrived) noexcept
{
    m_late.emplace_back(aArrived, aArrived > aOwnerTick ? aArrived - aOwnerTick : 0);
}

uint64_t CopyClock::Network() const noexcept
{
    uint64_t late = 0;
    for (const auto& [cArrived, cLate] : m_late)
        late = std::max(late, cLate);
    return std::min(late + kMarginMs, kMaxMs);
}

void CopyClock::Advance(uint64_t aNow) noexcept
{
    m_frameMs = m_started && aNow > m_now ? static_cast<double>(aNow - m_now) : 0.0;
    m_now = aNow;
    ++m_frame;
    while (!m_late.empty() && m_late.front().first + kWindowMs < aNow)
        m_late.pop_front();

    const uint64_t cDelay = Network();
    const double cTarget = aNow > cDelay ? static_cast<double>(aNow - cDelay) : 0.0;
    if (!m_started || cTarget > m_tick + kSnapMs)
    {
        m_tick = std::max(m_tick, cTarget);
        m_started = true;
        return;
    }
    const double cRate = std::clamp(1.0 + (cTarget - m_tick) / kSteerMs, kSlowest, kFastest);
    m_tick = std::max(m_tick, std::min(static_cast<double>(aNow), m_tick + m_frameMs * cRate));
}

void CopyClock::Reset() noexcept
{
    *this = {};
}

void CopyPlayback::OnPoint(uint64_t aTick) noexcept
{
    if (aTick <= m_last)
        return;
    if (m_last)
        m_gaps.emplace_back(aTick, std::min(aTick - m_last, kLongestGapMs));
    m_last = aTick;
    while (!m_gaps.empty() && m_gaps.front().first + CopyClock::kWindowMs < aTick)
        m_gaps.pop_front();
}

double CopyPlayback::TargetInterval() const noexcept
{
    if (m_gaps.empty())
        return kDefaultIntervalMs;
    double all = 0.0;
    double newest = 0.0;
    size_t count = 0;
    for (auto it = m_gaps.rbegin(); it != m_gaps.rend(); ++it, ++count)
    {
        all += static_cast<double>(it->second);
        if (count < kNewest)
            newest += static_cast<double>(it->second);
    }
    return std::max(all / static_cast<double>(m_gaps.size()), newest / static_cast<double>(std::min(m_gaps.size(), kNewest)));
}

uint64_t CopyPlayback::At(const CopyClock& acClock) noexcept
{
    if (m_started && m_frame == acClock.Frame())
        return m_tick;
    const double cTarget = TargetInterval();
    const double cStep = kSteer * acClock.FrameMs();
    m_interval = m_started ? m_interval + std::clamp(cTarget - m_interval, -cStep, cStep) : cTarget;
    const double cWanted = static_cast<double>(acClock.Now()) - m_interval;
    m_tick = std::max(m_tick, cWanted > 0.0 ? static_cast<uint64_t>(cWanted) : 0);
    m_frame = acClock.Frame();
    m_started = true;
    return m_tick;
}

void CopyPlayback::Snap(const CopyClock& acClock) noexcept
{
    m_interval = TargetInterval();
    const double cWanted = static_cast<double>(acClock.Now()) - m_interval;
    m_tick = cWanted > 0.0 ? static_cast<uint64_t>(cWanted) : 0;
    m_frame = acClock.Frame();
    m_started = true;
}
