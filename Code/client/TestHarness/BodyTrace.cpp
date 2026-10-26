#include "BodyTrace.h"

#include <Havok/bhkCollision.h>
#include <NetImmerse/NiNode.h>
#include <TESObjectREFR.h>

#include <chrono>
#include <utility>

namespace TestHarness
{
void BodyTrace::Watch(const std::set<uint32_t>& acReferences)
{
    m_references.insert(acReferences.begin(), acReferences.end());
}

nlohmann::json BodyTrace::Take()
{
    return std::exchange(m_entries, nlohmann::json::array());
}

nlohmann::json BodyTrace::Stop()
{
    m_references.clear();
    return Take();
}

void BodyTrace::Poll()
{
    const int64_t cNow = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const uint32_t cFormId : m_references)
    {
        auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        NiNode* p3D = pObject && !pObject->IsDeleted() ? pObject->GetNiNode() : nullptr;
        if (!p3D)
            continue;
        nlohmann::json bodies = nlohmann::json::array();
        for (bhkWorldObject* pBody : RigidBodiesOf(*p3D))
        {
            BodyRead read;
            if (!pBody || !ReadBody(*pBody, read))
                continue;
            bodies.push_back({{"position", {read.Position.x, read.Position.y, read.Position.z}},
                              {"motion", pBody->referencedObject->motionType},
                              {"awake", read.Active}});
        }
        const auto& cTranslate = p3D->world.translate;
        m_entries.push_back({{"t", cNow}, {"id", cFormId}, {"position_3d", {cTranslate.x, cTranslate.y, cTranslate.z}}, {"bodies", bodies}});
    }
}
} // namespace TestHarness
