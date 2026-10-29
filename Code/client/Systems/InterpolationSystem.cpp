#include <Systems/InterpolationSystem.h>
#include <Components.h>

#include <algorithm>
#include <cmath>

#include <World.h>

bool InterpolationSystem::Interpolate(const InterpolationComponent::TimePoint& acFirst, const InterpolationComponent::TimePoint& acSecond, const uint64_t aTick, glm::vec3& aPosition, glm::vec3& aRotation, uint64_t aAheadMs) noexcept
{
    constexpr float cPi = 3.14159265358979323846f;
    constexpr float cCircle = 2.f * cPi;
    float fraction = 0.f;
    const bool cStanding = !acFirst.Lying && !acSecond.Lying && !acFirst.Knocked && !acSecond.Knocked;
    if (aTick >= acSecond.Tick)
        fraction = 1.f;
    else if (aTick > acFirst.Tick && acSecond.Tick > acFirst.Tick && acFirst.Lying == acSecond.Lying)
        fraction = static_cast<float>(static_cast<double>(aTick - acFirst.Tick) / static_cast<double>(acSecond.Tick - acFirst.Tick));

    for (int axis = 0; axis < 3; ++axis)
    {
        aPosition[axis] = acFirst.Position[axis] + (acSecond.Position[axis] - acFirst.Position[axis]) * fraction;
        float difference = std::fmod(acSecond.Rotation[axis] - acFirst.Rotation[axis], cCircle);
        if (difference > cPi)
            difference -= cCircle;
        if (difference < -cPi)
            difference += cCircle;
        float angle = std::fmod(acFirst.Rotation[axis] + difference * fraction, cCircle);
        if (angle < 0.f)
            angle += cCircle;
        if (axis == 0 && angle > cPi / 2.f)
            angle -= cCircle;
        aRotation[axis] = angle;
    }

    // The next snapshot is late: on its way a little longer, its facing held.
    if (aTick > acSecond.Tick && aAheadMs && cStanding && acSecond.Tick > acFirst.Tick)
    {
        const float cAhead = static_cast<float>(std::min(aTick - acSecond.Tick, aAheadMs)) / static_cast<float>(acSecond.Tick - acFirst.Tick);
        aPosition = acSecond.Position + (acSecond.Position - acFirst.Position) * cAhead;
    }

    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(aPosition[axis]) || !std::isfinite(aRotation[axis]))
            return false;
    return true;
}

void InterpolationSystem::AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint) noexcept
{
    auto itor = std::begin(aInterpolationComponent.TimePoints);
    const auto end = std::cend(aInterpolationComponent.TimePoints);

    while (itor != end)
    {
        if (itor->Tick > acPoint.Tick)
        {
            aInterpolationComponent.TimePoints.insert(itor, acPoint);

            return;
        }

        ++itor;
    }

    aInterpolationComponent.TimePoints.push_back(acPoint);
}

uint64_t InterpolationSystem::PlaybackTick(World& aWorld, entt::entity aEntity) noexcept
{
    auto& clock = aWorld.GetCopyClock();
    if (auto* pInterpolation = aWorld.try_get<InterpolationComponent>(aEntity))
        return pInterpolation->Playback.At(clock);
    const auto cDefault = static_cast<uint64_t>(CopyPlayback::kDefaultIntervalMs);
    return clock.Now() > cDefault ? clock.Now() - cDefault : 0;
}

InterpolationComponent& InterpolationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    return aWorld.emplace_or_replace<InterpolationComponent>(aEntity);
}

void InterpolationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<InterpolationComponent>(aEntity))
        aWorld.remove<InterpolationComponent>(aEntity);
}
