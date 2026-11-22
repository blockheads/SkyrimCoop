// GuestEngine's half for the session's bodies (docs/design/physics-sync.md): a world state row's rest, this game's
// player's pushes and holds, and what this game's physics moves; IBodyBridge plays the others'.
#include <Bridges/GuestEngine.h>

#include <Bridges/IBodyBridge.h>

#include <Bridges/ReferenceBodies.h>
#include <Games/ReferenceStates.h>
#include <Games/Skyrim/PlayerHands.h>
#include <Havok/bhkCollision.h>
#include <Actor.h>
#include <Havok/Ragdoll.h>
#include <NetImmerse/NiNode.h>
#include <FormIds.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <map>
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
        if (m_bodyBridge.Settle(it->first))
            it = m_placed.erase(it);
        else
            ++it;
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

std::vector<BodyPush> GuestEngine::Keyed(const std::vector<PlayerHands::Push>& acPushes) const noexcept
{
    std::vector<BodyPush> pushes;
    for (const auto& cPush : acPushes)
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

std::vector<BodyPush> GuestEngine::TakePushes() noexcept
{
    return Keyed(PlayerHands::Take());
}

std::vector<BodyPush> GuestEngine::TakeHits() noexcept
{
    return Keyed(PlayerHands::TakeHits());
}

GuestEngine::OwnBodies GuestEngine::TakeOwnBodies(uint64_t aTick, const SessionOf_t& acSessionOf) noexcept
{
    OwnBodies own;
    const auto cUpdate = m_moving.Take(aTick);
    // A copy's corpse this game's player moved: its bodies as an object's, by the copy's session id.
    const auto cCorpse = [&acSessionOf](const PhysicsBodies::Bodies& acBodies) -> uint32_t {
        return acBodies.Actor && acBodies.FormId != 0x14 && acSessionOf ? acSessionOf(acBodies.FormId) : 0;
    };
    for (const auto& cAwake : cUpdate.Awake)
    {
        if (const uint32_t cSession = cCorpse(cAwake))
        {
            for (BodyFrame frame : cAwake.Read)
            {
                frame.Key = {.Actor = cSession, .Index = frame.Key.Index};
                own.Objects.push_back(frame);
            }
            continue;
        }
        if (cAwake.Actor)
        {
            own.Ragdoll.insert(own.Ragdoll.end(), cAwake.Read.begin(), cAwake.Read.end());
            continue;
        }
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cAwake.FormId));
        if (const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt)
            for (const auto& cRead : cAwake.Read)
            {
                BodyFrame frame = cRead;
                frame.Key = {.Object = *cId, .Index = cRead.Key.Index};
                own.Moved.push_back({.Key = frame.Key, .Position = frame.Position, .Rotation = frame.Rotation, .Knocked = frame.Knocked});
                own.Objects.push_back(frame);
            }
    }
    for (const auto& cRested : cUpdate.Rested)
    {
        if (const uint32_t cSession = cCorpse(cRested))
        {
            for (const auto& cRead : cRested.Read)
                own.ObjectsRested.push_back({.Key = {.Actor = cSession, .Index = cRead.Key.Index}, .Position = cRead.Position, .Rotation = cRead.Rotation});
            continue;
        }
        if (cRested.Actor)
        {
            for (const auto& cRead : cRested.Read)
                own.RagdollRested.push_back({.Key = cRead.Key, .Position = cRead.Position, .Rotation = cRead.Rotation, .Knocked = cRead.Knocked});
            continue;
        }
        const auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cRested.FormId));
        if (const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt)
            for (const auto& cRead : cRested.Read)
                own.ObjectsRested.push_back({.Key = {.Object = *cId, .Index = cRead.Key.Index}, .Position = cRead.Position, .Rotation = cRead.Rotation});
    }
    (void)m_moving.TakeRested();
    return own;
}

std::vector<GuestEngine::IslandBody> GuestEngine::ReadIslands(const std::vector<BodyKey>& acKeys) noexcept
{
    std::vector<IslandBody> bodies;
    std::map<const hkpRigidBody*, uint32_t> islandOf;
    const auto cAdd = [&](hkpRigidBody& aBody, uint32_t aIsland) {
        TESObjectREFR* pObject = ReferenceOf(aBody);
        if (!pObject || Cast<Actor>(pObject) || !aBody.userData)
            return;
        const auto cId = HostIdOf(*pObject);
        const auto cIndex = cId ? ReferenceBodies::IndexOf(*pObject, &aBody) : std::nullopt;
        if (BodyRead read; cIndex && ReadBody(*aBody.userData, read) && read.Dynamic)
            bodies.push_back({{.Key = {.Object = *cId, .Index = *cIndex}, .Position = read.Position, .Rotation = read.Rotation}, aIsland});
    };
    for (const BodyKey& cKey : acKeys)
    {
        const uint32_t cFormId = cKey.Actor ? 0 : m_bodyBridge.ReferenceFormId(cKey);
        const auto cOwn = ReferenceBodies::At(cFormId ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr, cKey.Index);
        hkpRigidBody* pOwn = cOwn.pBody ? cOwn.pBody->referencedObject : nullptr;
        // One island's bodies once, whichever of its own bodies named it.
        if (!pOwn || islandOf.contains(pOwn) || !pOwn->IsActive())
            continue;
        const auto cIsland = static_cast<uint32_t>(islandOf.size());
        islandOf[pOwn] = cIsland;
        cAdd(*pOwn, cIsland);
        for (hkpRigidBody* pMate : IslandMatesOf(*cOwn.pBody))
            if (islandOf.try_emplace(pMate, cIsland).second)
                cAdd(*pMate, cIsland);
    }
    return bodies;
}

void GuestEngine::FollowOwn(const BodyKey& acKey) noexcept
{
    if (const uint32_t cFormId = acKey.Actor ? 0 : m_bodyBridge.ReferenceFormId(acKey))
    {
        m_moving.Watch(cFormId);
        (void)m_moving.Follow(cFormId);
    }
}

std::vector<BodyFrame> GuestEngine::ReadHeld(const SessionOf_t& acSessionOf) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectREFR* pObject = pPlayer && pPlayer->grabbedObject ? TESObjectREFR::GetByHandle(pPlayer->grabbedObject) : nullptr;
    // A copy's corpse: its ragdoll's bodies by the copy's session id, watched until they rest after the let go.
    if (const auto* pCopy = Cast<Actor>(pObject); pCopy && pCopy->IsDead() && acSessionOf)
    {
        const uint32_t cSession = acSessionOf(pCopy->formID);
        if (!cSession)
            return {};
        m_moving.Watch(pCopy->formID);
        std::vector<BodyFrame> frames;
        const auto cBodies = RagdollBodiesOf(*pCopy);
        for (size_t i = 0; i < cBodies.size(); ++i)
            if (BodyRead read; cBodies[i] && ReadBody(*cBodies[i], read) && read.Dynamic)
                frames.push_back({.Key = {.Actor = cSession, .Index = static_cast<uint8_t>(i)}, .Position = read.Position, .Rotation = read.Rotation,
                                  .LinearVelocity = read.LinearVelocity, .AngularVelocity = read.AngularVelocity});
        return frames;
    }
    const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt;
    if (!cId)
        return {};
    auto frames = ReferenceBodies::ReadFrames(*pObject);
    for (BodyFrame& frame : frames)
        frame.Key.Object = *cId;
    return frames;
}
