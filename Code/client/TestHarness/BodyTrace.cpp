#include "BodyTrace.h"
#include "EngineCalls.h"

#include <Actor.h>
#include <Havok/Ragdoll.h>
#include <Havok/bhkCollision.h>
#include <Misc/BSFixedString.h>
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

namespace
{
nlohmann::json NodeJson(NiNode& aRoot, const BSFixedString& acName)
{
    const NiAVObject* pNode = aRoot.GetByName(acName);
    if (!pNode)
        return nullptr;
    return {pNode->world.translate.x, pNode->world.translate.y, pNode->world.translate.z};
}
} // namespace

void BodyTrace::Poll()
{
    const int64_t cNow = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const uint32_t cFormId : m_references)
    {
        auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cFormId));
        NiNode* p3D = pObject && !pObject->IsDeleted() ? pObject->GetNiNode() : nullptr;
        if (!p3D)
            continue;
        // An actor's ragdoll in bone order (Ragdoll.h), with its skeleton's pelvis and head (what a person sees).
        const auto* pActor = Cast<Actor>(pObject);
        nlohmann::json bodies = nlohmann::json::array();
        for (bhkWorldObject* pBody : pActor ? RagdollBodiesOf(*pActor) : RigidBodiesOf(*p3D))
        {
            BodyRead read;
            if (!pBody || !pBody->referencedObject || !ReadBody(*pBody, read))
            {
                bodies.push_back(nullptr);
                continue;
            }
            nlohmann::json body = {{"position", {read.Position.x, read.Position.y, read.Position.z}},
                                   {"velocity", {read.LinearVelocity.x, read.LinearVelocity.y, read.LinearVelocity.z}},
                                   {"motion", pBody->referencedObject->motionType},
                                   {"awake", read.Active}};
            // The bone it moves: where a person sees that part of the body.
            if (float bone[3]; pActor && EngineCalls::BoneOf(*pBody, bone))
                body["bone"] = {bone[0], bone[1], bone[2]};
            bodies.push_back(std::move(body));
        }
        const auto& cTranslate = p3D->world.translate;
        nlohmann::json entry = {{"t", cNow}, {"id", cFormId}, {"position_3d", {cTranslate.x, cTranslate.y, cTranslate.z}}, {"bodies", bodies}};
        if (pActor)
        {
            static const BSFixedString s_pelvis("NPC Pelvis [Pelv]");
            static const BSFixedString s_head("NPC Head [Head]");
            entry["pelvis"] = NodeJson(*p3D, s_pelvis);
            entry["head"] = NodeJson(*p3D, s_head);
            entry["position"] = {pActor->position.x, pActor->position.y, pActor->position.z};
            entry["angle"] = pActor->rotation.z;
            entry["dead"] = pActor->IsDead();
            entry["ragdolled"] = EngineCalls::IsInRagdollState(cFormId);
            entry["knock"] = pActor->actorState.GetKnockState();
            entry["life"] = ActorState::LifeStateOf(pActor->actorState.flags1); // ACTOR_LIFE_STATE: 1 dying, 2 dead
        }
        m_entries.push_back(std::move(entry));
    }
}
} // namespace TestHarness
