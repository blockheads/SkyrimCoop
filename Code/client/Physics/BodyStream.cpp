#include <Physics/BodyStream.h>

#include <Physics/BodyPrediction.h>

#include <algorithm>

namespace
{
constexpr float kChanged = 1e9f;

// What the receiver must hear again: who, the numbers, held, at rest, or a rest somewhere else.
bool Changed(const BodyState& acSent, const BodyState& acNow) noexcept
{
    if (acSent.Author != acNow.Author || acSent.AuthSeq != acNow.AuthSeq || acSent.HoldSeq != acNow.HoldSeq || acSent.Held != acNow.Held ||
        acSent.AtRest != acNow.AtRest || acSent.Placed != acNow.Placed)
        return true;
    if (!acNow.AtRest)
        return false;
    const auto cApart = BodyPrediction::Between(acSent.Frame, acNow.Frame);
    return cApart.Distance >= BodyTolerances::kSameDistance || cApart.Angle >= BodyTolerances::kSameAngle;
}
} // namespace

BodyStream::BodyStream(const Limits& acLimits) noexcept
    : m_limits(acLimits)
    , m_allowance(acLimits.BurstBytes)
{
}

std::vector<BodyState> BodyStream::Due(const std::vector<BodyState>& acStates, uint64_t aTick) noexcept
{
    if (m_lastTick && aTick > m_lastTick)
        m_allowance = std::min(m_limits.BurstBytes, m_allowance + m_limits.BytesPerSecond * static_cast<float>(aTick - m_lastTick) / 1000.f);
    m_lastTick = aTick;

    struct Candidate
    {
        const BodyState* pState;
        float Priority;
    };
    std::vector<Candidate> candidates;
    for (const BodyState& cState : acStates)
    {
        auto it = m_sent.find(cState.Frame.Key);
        if (it == m_sent.end())
        {
            candidates.push_back({&cState, kChanged});
            continue;
        }
        Sent& sent = it->second;
        if (Changed(sent.State, cState))
            sent.Repeats = BodyTolerances::kChangedRepeats;
        if (sent.Repeats)
        {
            candidates.push_back({&cState, kChanged});
            continue;
        }
        const float cAge = static_cast<float>(aTick - std::min(aTick, sent.Tick));
        float priority;
        if (cState.AtRest)
            priority = cAge / static_cast<float>(BodyTolerances::kRestRefreshMs);
        else if (cState.Frame.Gravity)
        {
            const auto cError = BodyPrediction::Between(BodyPrediction::Predict(sent.State.Frame, cAge / 1000.f), cState.Frame);
            priority = std::max({cError.Distance / BodyTolerances::kFallDistance, cError.Angle / BodyTolerances::kFallAngle,
                                 cAge / static_cast<float>(BodyTolerances::kRefreshMs)});
        }
        else
        {
            // On the ground: steadily while it moves (the further since the last one, the sooner), else a refresh.
            const auto cMoved = BodyPrediction::Between(sent.State.Frame, cState.Frame);
            // Its stop is a move too: the state it was last sent still had it going.
            const bool cStopped = glm::length(sent.State.Frame.LinearVelocity - cState.Frame.LinearVelocity) >= BodyTolerances::kMovedSpeed;
            const bool cMoves = cMoved.Distance >= BodyTolerances::kMovedDistance || cMoved.Angle >= BodyTolerances::kMovedAngle || cStopped ||
                                sent.State.Frame.Gravity;
            priority = cMoves ? cAge / static_cast<float>(BodyTolerances::kMovingMs) * (1.f + cMoved.Distance)
                              : cAge / static_cast<float>(BodyTolerances::kRefreshMs);
            // A knock or a bounce goes at once: its start is what the others' peak is made of.
            const bool cKnocked = glm::length(sent.State.Frame.LinearVelocity - cState.Frame.LinearVelocity) >= BodyTolerances::kKnockSpeed;
            if (cKnocked)
                priority = std::max(priority, 1.f);
            else if (cMoves && cAge < static_cast<float>(BodyTolerances::kMovingMs))
                priority = 0.f;
        }
        if (priority >= 1.f)
            candidates.push_back({&cState, priority});
    }
    std::ranges::sort(candidates, [](const Candidate& acA, const Candidate& acB) { return acA.Priority > acB.Priority; });

    std::vector<BodyState> due;
    for (const auto& cCandidate : candidates)
    {
        const auto cBytes = static_cast<float>(cCandidate.pState->WireBytes());
        if (m_allowance < cBytes)
            break;
        m_allowance -= cBytes;
        const BodyState cState = cCandidate.pState->Quantized();
        const auto [it, cNew] = m_sent.try_emplace(cState.Frame.Key);
        Sent& sent = it->second;
        if (cNew)
            sent.Repeats = BodyTolerances::kChangedRepeats;
        sent.State = cState;
        sent.Tick = aTick;
        if (sent.Repeats)
            --sent.Repeats;
        due.push_back(cState);
    }
    return due;
}
