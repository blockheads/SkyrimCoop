#include "BodyTrace.h"
#include "EngineCalls.h"

#include <Actor.h>
#include <Havok/Ragdoll.h>
#include <Havok/bhkCollision.h>
#include <Misc/BSFixedString.h>
#include <NetImmerse/BSGeometry.h>
#include <NetImmerse/BSLightingShaderMaterial.h>
#include <NetImmerse/BSLightingShaderProperty.h>
#include <NetImmerse/BSShaderProperty.h>
#include <NetImmerse/NiNode.h>
#include <RTTI.h>
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

nlohmann::json GeometryJson(NiAVObject& aRoot)
{
    auto entries = nlohmann::json::array();
    std::vector<NiAVObject*> queue{&aRoot};
    for (size_t i = 0; i < queue.size(); ++i)
    {
        NiAVObject* pObject = queue[i];
        if (auto* pGeometry = Cast<BSGeometry>(pObject))
        {
            nlohmann::json entry = {{"node", pObject->name ? pObject->name : ""}, {"flags", pObject->flags},
                                    {"vertex_desc", pGeometry->vertexDesc}, {"scale", pObject->world.scale},
                                    {"position", {pObject->world.translate.x, pObject->world.translate.y, pObject->world.translate.z}},
                                    {"previous_position", {pObject->previousWorld.translate.x, pObject->previousWorld.translate.y, pObject->previousWorld.translate.z}},
                                    {"previous_scale", pObject->previousWorld.scale}, {"updated_frame", pObject->lastUpdatedFrame},
                                    {"rotation", pObject->world.rotate}, {"local_rotation", pObject->local.rotate}};
            if (const auto* pShader = pGeometry->shaderProperty.object)
            {
                entry["shader"] = {{"flags", pShader->flags}, {"alpha", pShader->alpha},
                                    {"render_state", pShader->lastRenderPassState},
                                    {"material", reinterpret_cast<uintptr_t>(pShader->material)},
                                    {"effect_data", reinterpret_cast<uintptr_t>(pShader->effectData)},
                                    {"passes", reinterpret_cast<uintptr_t>(pShader->renderPassList.head)},
                                    {"debug_passes", reinterpret_cast<uintptr_t>(pShader->debugRenderPassList.head)},
                                    {"accumulated_frame", pShader->lastAccumulatedFrameCount}};
                if (Cast<BSLightingShaderProperty>(pGeometry->shaderProperty.object) && pShader->material)
                {
                    const auto& cMaterial = *static_cast<const BSLightingShaderMaterial*>(pShader->material);
                    entry["textures"] = {{"offset", cMaterial.texCoordOffset}, {"scale", cMaterial.texCoordScale},
                                          {"diffuse", reinterpret_cast<uintptr_t>(cMaterial.diffuseTexture)},
                                          {"normal", reinterpret_cast<uintptr_t>(cMaterial.normalTexture)},
                                          {"render_target", cMaterial.diffuseRenderTargetSourceIndex}};
                }
            }
            entries.push_back(std::move(entry));
        }
        if (auto* pNode = Cast<NiNode>(pObject))
            for (uint16_t child = 0; child < pNode->children.length; ++child)
                if (auto* pChild = pNode->children.data[child])
                    queue.push_back(pChild);
    }
    return entries;
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
        // Its 3D's flags (NiAVObject): kHidden 1, kNotVisible 1 << 20 (the last frame's culling: not drawn).
        entry["flags_3d"] = p3D->flags;
        if (!pActor)
            entry["geometry"] = GeometryJson(*p3D);
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
