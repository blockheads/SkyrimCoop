#include <Physics/BodyMotions.h>

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
