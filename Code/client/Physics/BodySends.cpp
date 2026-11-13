#include <Physics/BodySends.h>

#include <algorithm>

BodySends::BodySends(const Limits& acLimits) noexcept
    : m_limits(acLimits)
    , m_allowance(acLimits.BurstBytes)
{
}

std::vector<BodyFrame> BodySends::Due(const std::vector<BodyFrame>& acAwake, uint64_t aTick, const std::vector<BodyKey>& acHeld) noexcept
{
    if (m_lastTick && aTick > m_lastTick)
        m_allowance = std::min(m_limits.BurstBytes, m_allowance + m_limits.BytesPerSecond * static_cast<float>(aTick - m_lastTick) / 1000.f);
    m_lastTick = aTick;

    struct Candidate
    {
        const BodyFrame* pFrame;
        float Priority;
    };
    std::vector<Candidate> candidates;
    for (const auto& cFrame : acAwake)
    {
        const bool cHeld = std::ranges::find(acHeld, cFrame.Key) != acHeld.end();
        auto it = m_sent.find(cFrame.Key);
        // Kept, then grabbed: other games must keyframe the hold now.
        if (it != m_sent.end() && it->second.Kept && cHeld)
        {
            m_sent.erase(it);
            it = m_sent.end();
        }
        if (it == m_sent.end())
        {
            if (m_limits.StillStaysPut && !cHeld && Still(cFrame))
            {
                // Woken still (physics.md, "Woken still"): the guest's guess is its rest, here.
                BodyFrame rest = cFrame.Quantized();
                rest.LinearVelocity = rest.AngularVelocity = {};
                rest.Gravity = false;
                m_sent[cFrame.Key] = {rest, aTick, true};
                continue;
            }
            // Never sent: the guest shows it where it was.
            candidates.push_back({&cFrame, 1e9f});
            continue;
        }
        const auto& cSent = it->second;
        const float cSeconds = static_cast<float>(aTick - std::min(aTick, cSent.Tick)) / 1000.f;
        const auto cError = BodyPrediction::Between(BodyPrediction::Predict(cSent.Frame, cSeconds), cFrame);
        const float cWrong = std::max(cError.Distance / m_limits.Distance, cError.Angle / m_limits.Angle);
        // A kept rest has no frame to lose.
        const float cAge = cSent.Kept ? 0.f : cSeconds * 1000.f / static_cast<float>(m_limits.RefreshMs);
        if (cWrong >= 1.f || cAge >= 1.f)
            candidates.push_back({&cFrame, std::max(cWrong, cAge)});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& acA, const Candidate& acB) { return acA.Priority > acB.Priority; });

    std::vector<BodyFrame> due;
    for (const auto& cCandidate : candidates)
    {
        if (m_allowance < static_cast<float>(BodyFrame::kWireBytes))
            break;
        m_allowance -= static_cast<float>(BodyFrame::kWireBytes);
        const BodyFrame cFrame = cCandidate.pFrame->Quantized();
        m_sent[cFrame.Key] = {cFrame, aTick};
        due.push_back(cFrame);
    }
    return due;
}

bool BodySends::Still(const BodyFrame& acFrame) const noexcept
{
    // Still: a refresh's worth of its own motion stays inside the send limits.
    BodyFrame frame = acFrame;
    frame.Gravity = false;
    const auto cDrift = BodyPrediction::Between(BodyPrediction::Predict(frame, static_cast<float>(m_limits.RefreshMs) / 1000.f), frame);
    return cDrift.Distance < m_limits.Distance && cDrift.Angle < m_limits.Angle;
}

void BodySends::Forget(const BodyKey& acKey) noexcept
{
    m_sent.erase(acKey);
}

void BodyMotions::Mark(std::vector<BodyFrame>& aAwake, uint64_t aTick) noexcept
{
    for (auto& frame : aAwake)
    {
        auto [it, cAdded] = m_last.try_emplace(frame.Key, Last{frame.LinearVelocity, aTick, false});
        Last& last = it->second;
        // A second read of the same tick keeps the last answer.
        if (!cAdded && aTick > last.Tick)
        {
            last.Gravity = BodyPrediction::FallsFreely(last.Velocity, frame.LinearVelocity, static_cast<float>(aTick - last.Tick) / 1000.f);
            last.Velocity = frame.LinearVelocity;
            last.Tick = aTick;
        }
        frame.Gravity = last.Gravity;
    }
}

void BodyMotions::Forget(const BodyKey& acKey) noexcept
{
    m_last.erase(acKey);
}
