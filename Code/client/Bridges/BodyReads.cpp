#include <Bridges/BodyReads.h>

#include <Actor.h>
#include <Forms/FormChanges.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <NetImmerse/NiNode.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <mutex>
#include <unordered_set>
#include <RTTI.h>

namespace
{
// The references the funnel saw physics move since the last update, filled on the engine's threads. A leaf lock.
std::mutex s_movedLock;
std::unordered_set<uint32_t> s_moved;

// The havok move flag: every frame while a body is awake.
void OnFormChange(const TESForm& acForm, uint32_t aFlags, bool, bool aRecorded) noexcept
{
    if (!aRecorded || !(aFlags & TESObjectREFR::CHANGE_REFR_HAVOK_MOVE) ||
        (acForm.formType != FormType::Reference && acForm.formType != FormType::Character))
        return;
    std::scoped_lock _(s_movedLock);
    s_moved.insert(acForm.formID);
}

// Its node's world pose; its rotation (row-major) as a quaternion, as a body reads.
std::pair<glm::vec3, glm::quat> NodePose(const NiAVObject& acNode) noexcept
{
    glm::mat3 rotation;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            rotation[column][row] = acNode.world.rotate[row][column];
    const auto& cAt = acNode.world.translate;
    return {glm::vec3(cAt.x, cAt.y, cAt.z), glm::quat_cast(rotation)};
}

TESObjectREFR* Loaded(uint32_t aFormId, NiNode*& apNode) noexcept
{
    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    apNode = pObject && !pObject->IsDeleted() ? pObject->GetNiNode() : nullptr;
    return apNode ? pObject : nullptr;
}
} // namespace

BodyReads::BodyReads() noexcept
{
    if (!FormChanges::Watch(&OnFormChange))
        spdlog::error("{}: no slot left to watch the change funnel, physics stays in this game", __FUNCTION__);
}

BodyReads::~BodyReads() noexcept
{
    FormChanges::Unwatch(&OnFormChange);
    std::scoped_lock _(s_movedLock);
    s_moved.clear();
}

std::vector<uint32_t> BodyReads::TakeMovedReferences() noexcept
{
    std::unordered_set<uint32_t> moved;
    {
        std::scoped_lock _(s_movedLock);
        moved.swap(s_moved);
    }
    return {moved.begin(), moved.end()};
}

uint32_t BodyReads::PlayerInRagdoll() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer && (pPlayer->IsDead() || pPlayer->actorState.IsKnockedDown()) ? pPlayer->formID : 0;
}

std::optional<IBodyReads::Reference> BodyReads::ReadReference(uint32_t aFormId, bool aBodies) noexcept
{
    NiNode* p3D = nullptr;
    TESObjectREFR* pObject = Loaded(aFormId, p3D);
    if (!pObject)
        return std::nullopt;
    const auto* pActor = Cast<Actor>(pObject);
    Reference reference{.Actor = pActor != nullptr,
                        .Dead = pActor && pActor->IsDead(),
                        .Up = pActor && !pActor->IsDead() && !pActor->actorState.IsKnockedDown(),
                        .Player = pObject == PlayerCharacter::Get(),
                        .Position = {pObject->position.x, pObject->position.y, pObject->position.z},
                        .Rotation = {pObject->rotation.x, pObject->rotation.y, pObject->rotation.z}};
    if (!aBodies)
        return reference;
    if (pActor)
    {
        for (bhkWorldObject* pBody : RagdollBodiesOf(*pObject))
            if (BodyRead read; pBody && ReadBody(*pBody, read))
                reference.Bodies.push_back(Body{.Read = read});
            else
                reference.Bodies.push_back(std::nullopt);
        return reference;
    }
    for (bhkNiCollisionObject* pCollision : CollisionObjectsOf(*p3D))
        if (BodyRead read; pCollision->body && ReadBody(*pCollision->body, read))
            reference.Bodies.push_back(Body{.Read = read, .Node = pCollision->sceneObject
                                                                      ? std::optional(NodePose(*pCollision->sceneObject))
                                                                      : std::nullopt});
        else
            reference.Bodies.push_back(std::nullopt);
    return reference;
}

void BodyReads::WriteNode(uint32_t aFormId, uint8_t aIndex) noexcept
{
    NiNode* p3D = nullptr;
    if (!Loaded(aFormId, p3D) || Cast<Actor>(TESForm::GetById(aFormId)))
        return;
    const auto cCollisions = CollisionObjectsOf(*p3D);
    if (aIndex < cCollisions.size())
        FollowBody(*cCollisions[aIndex]);
}
