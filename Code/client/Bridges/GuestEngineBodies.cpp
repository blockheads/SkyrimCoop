// GuestEngine's half for the session's bodies (docs/design/physics-sync.md): a world state row's rest, this game's
// player's pushes and holds, and what this game's physics moves; IBodyBridge plays the others'.
#include <Bridges/GuestEngine.h>

#include <Bridges/IBodyBridge.h>

#include <Bridges/ReferenceBodies.h>
#include <Games/ReferenceStates.h>
#include <Games/Skyrim/PlayerHands.h>
#include <Havok/bhkCollision.h>
#include <NetImmerse/NiNode.h>
#include <FormIds.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <RTTI.h>

bool GuestEngine::PlaceAtHostRest(TESObjectREFR& aObject, const ObjectChange& acMove) noexcept
{
    if (!ReferenceStates::Apply(aObject, acMove))
        return false;
    // This game's own flight of it ends there: held, so the engine puts its bodies at its node (FreePlaced frees them).
    NiNode* p3D = aObject.GetNiNode();
    if (!p3D)
        return true;
    const auto cCollisions = CollisionObjectsOf(*p3D);
    for (size_t i = 0; i < cCollisions.size(); ++i)
    {
        const BodyKey cKey{.Object = acMove.Object, .Index = static_cast<uint8_t>(i)};
        if (m_bodyBridge.KeepMotion(cKey) && std::ranges::find(m_placed, cKey, &std::pair<BodyKey, uint32_t>::first) == m_placed.end())
            m_placed.emplace_back(cKey, 0);
    }
    HoldBodies(*p3D);
    return true;
}

void GuestEngine::FreePlaced() noexcept
{
    for (auto it = m_placed.begin(); it != m_placed.end();)
    {
        // The node update after the hold moves the keyframed body there (bhkNiCollisionObject::UpdateWorldData 78378).
        if (++it->second < 2)
        {
            ++it;
            continue;
        }
        m_bodyBridge.Settle(it->first);
        it = m_placed.erase(it);
    }
}

std::optional<GameId> GuestEngine::HostIdOf(const TESObjectREFR& acObject) const noexcept
{
    if (!IsCreatedForm(acObject.formID))
        return ToSession(acObject.formID);
    for (const auto& [cHostId, cHandle] : m_created)
        if (TESObjectREFR::GetByHandle(cHandle) == &acObject)
            return GameId(ObjectChange::kCreatedModId, cHostId);
    return std::nullopt;
}

std::vector<BodyPush> GuestEngine::TakePushes() noexcept
{
    std::vector<BodyPush> pushes;
    for (const auto& cPush : PlayerHands::Take())
    {
        if (cPush.Impulse == glm::vec3(0.f))
            continue;
        auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cPush.Object));
        const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt;
        const auto cIndex = cId ? ReferenceBodies::IndexOf(*pObject, cPush.pBody) : std::nullopt;
        if (!cIndex)
            continue;
        const BodyKey cKey{.Object = *cId, .Index = *cIndex};
        // One per body: the frame's impulses summed, at the last one's point.
        if (auto it = std::ranges::find(pushes, cKey, &BodyPush::Key); it != pushes.end())
        {
            it->Impulse += cPush.Impulse;
            it->Point = cPush.Point;
        }
        else
            pushes.push_back({cKey, cPush.Impulse, cPush.Point});
    }
    return pushes;
}

GuestEngine::OwnBodies GuestEngine::TakeOwnBodies() noexcept
{
    OwnBodies own;
    const auto cUpdate = m_moving.Take();
    for (const auto& cAwake : cUpdate.Awake)
    {
        if (cAwake.Actor)
        {
            own.Ragdoll.insert(own.Ragdoll.end(), cAwake.Read.begin(), cAwake.Read.end());
            continue;
        }
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cAwake.FormId));
        if (const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt)
            for (const auto& cRead : cAwake.Read)
                own.Moved.push_back({.Key = {.Object = *cId, .Index = cRead.Key.Index}, .Position = cRead.Position, .Rotation = cRead.Rotation});
    }
    for (const auto& cRested : cUpdate.Rested)
        if (cRested.Actor)
            for (const auto& cRead : cRested.Read)
                own.RagdollRested.push_back({.Key = cRead.Key, .Position = cRead.Position, .Rotation = cRead.Rotation});
    (void)m_moving.TakeRested();
    return own;
}

std::vector<BodyFrame> GuestEngine::ReadHeld() const noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectREFR* pObject = pPlayer && pPlayer->grabbedObject ? TESObjectREFR::GetByHandle(pPlayer->grabbedObject) : nullptr;
    const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt;
    if (!cId)
        return {};
    auto frames = ReferenceBodies::ReadFrames(*pObject);
    for (BodyFrame& frame : frames)
        frame.Key.Object = *cId;
    return frames;
}
