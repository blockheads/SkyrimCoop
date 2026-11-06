#include "GaitWatch.h"

#include <Actor.h>
#include <Misc/BSFixedString.h>
#include <NetImmerse/NiNode.h>
#include <Bridges/IActorBridge.h>
#include <World.h>

#include <unordered_set>

namespace TestHarness
{
namespace
{
constexpr auto cKept = std::chrono::seconds(5);
} // namespace

void GaitWatch::Poll()
{
    static const BSFixedString s_feet[2] = {BSFixedString("NPC L Foot [Lft ]"), BSFixedString("NPC R Foot [Rft ]")};
    const auto cNow = std::chrono::steady_clock::now();
    std::unordered_set<uint32_t> seen;
    for (const uint32_t cFormId : World::Get().GetActorBridge().GetLoadedActorFormIds())
    {
        auto* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        NiNode* p3D = pActor && !pActor->IsDeleted() ? pActor->GetNiNode() : nullptr;
        if (!p3D)
            continue;
        GaitSample sample{cNow, {pActor->position.x, pActor->position.y, pActor->position.z}, {}, !(p3D->flags & NiAVObject::kNotVisible)};
        bool humanoid = true;
        for (int i = 0; i < 2 && humanoid; ++i)
        {
            const NiAVObject* pFoot = p3D->GetByName(s_feet[i]);
            humanoid = pFoot != nullptr;
            if (pFoot)
            {
                sample.Feet[i][0] = pFoot->world.translate.x - p3D->world.translate.x;
                sample.Feet[i][1] = pFoot->world.translate.y - p3D->world.translate.y;
                sample.Feet[i][2] = pFoot->world.translate.z - p3D->world.translate.z;
            }
        }
        if (!humanoid)
            continue;
        seen.insert(cFormId);
        auto& samples = m_samples[cFormId];
        samples.push_back(sample);
        while (!samples.empty() && cNow - samples.front().Time > cKept)
            samples.pop_front();
    }
    std::erase_if(m_samples, [&seen](const auto& acEntry) { return !seen.contains(acEntry.first); });
}

nlohmann::json GaitWatch::Read(double aSeconds) const
{
    const auto cFrom = std::chrono::steady_clock::now() - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(aSeconds));
    nlohmann::json actors = nlohmann::json::object();
    for (const auto& [cFormId, cSamples] : m_samples)
    {
        const GaitReading cReading = MeasureGait(cSamples, cFrom);
        actors[std::to_string(cFormId)] = {{"travelled", cReading.Travelled}, {"feet", cReading.Feet}, {"seconds", cReading.Seconds},
                                           {"frames", cReading.Frames}, {"hidden_frames", cReading.HiddenFrames}};
    }
    return actors;
}
} // namespace TestHarness
