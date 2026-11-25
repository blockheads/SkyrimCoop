#pragma once

#include <Physics/BodyPrediction.h>
#include <Physics/BodyTolerances.h>
#include <Structs/BodyFrame.h>

#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

// Where each body Havok pairs with one of this game's own lay when first paired (an AABB overlap, 64648): it is
// knocked once it leaves that pose by a settle (kSameDistance, kSameAngle). No engine: UnitTests.
class PairedPoses
{
public:
    // This update's pairs' other bodies, each with its pose now (none: not here); a body no longer paired is forgotten.
    // acRestOf: its last known rest, its first lay when within kSendDistance (a shift before the first read is still one).
    void Update(const std::vector<std::pair<BodyKey, std::optional<BodyPose>>>& acPaired,
                const std::function<std::optional<BodyPose>(const BodyKey&)>& acRestOf = {}) noexcept
    {
        std::map<BodyKey, BodyPose> kept;
        for (const auto& [cKey, cNow] : acPaired)
        {
            if (!cNow)
                continue;
            const auto it = m_lay.find(cKey);
            if (it != m_lay.end())
                kept[cKey] = it->second;
            else
            {
                const auto cRest = acRestOf ? acRestOf(cKey) : std::nullopt;
                kept[cKey] = cRest && glm::distance(cRest->Position, cNow->Position) <= BodyTolerances::kSendDistance ? *cRest : *cNow;
            }
            m_now[cKey] = *cNow;
        }
        m_lay = std::move(kept);
    }
    [[nodiscard]] bool Moved(const BodyKey& acKey) const noexcept
    {
        const auto cLay = m_lay.find(acKey);
        const auto cNow = m_now.find(acKey);
        if (cLay == m_lay.end() || cNow == m_now.end())
            return false;
        const auto cApart = BodyPrediction::Between({.Position = cLay->second.Position, .Rotation = cLay->second.Rotation},
                                                    {.Position = cNow->second.Position, .Rotation = cNow->second.Rotation});
        return cApart.Distance >= BodyTolerances::kSameDistance || cApart.Angle >= BodyTolerances::kSameAngle;
    }

private:
    std::map<BodyKey, BodyPose> m_lay;
    std::map<BodyKey, BodyPose> m_now;
};
