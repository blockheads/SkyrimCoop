// HostEngine's half for the host's bodies (docs/design/physics-sync.md): read for the guests.
#include <Bridges/HostEngine.h>

#include <Bridges/IBodyBridge.h>

#include <Bridges/ReferenceBodies.h>
#include <FormIds.h>
#include <Games/Skyrim/PlayerHands.h>
#include <TESObjectREFR.h>

#include <RTTI.h>

#include <algorithm>

HostBodyReads HostEngine::TakeBodies(uint64_t aTick, const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept
{
    HostBodyReads bodies;
    const auto cOwner = [&](const PhysicsBodies::Bodies& acBodies) -> std::optional<BodyKey> {
        if (acBodies.Actor)
        {
            const uint32_t cSessionId = aSessionOf ? aSessionOf(acBodies.FormId) : 0;
            return cSessionId ? std::optional<BodyKey>(BodyKey{.Actor = cSessionId}) : std::nullopt;
        }
        const auto cId = IsShared(acBodies.FormId) ? ToSessionOrCreated(acBodies.FormId) : std::nullopt;
        return cId ? std::optional<BodyKey>(BodyKey{.Object = *cId}) : std::nullopt;
    };
    const auto cUpdate = m_bodies.Take(aTick);
    for (const auto& cAwake : cUpdate.Awake)
        if (const auto cKey = cOwner(cAwake))
            for (BodyFrame frame : cAwake.Read)
            {
                frame.Key = {cKey->Object, cKey->Actor, frame.Key.Index};
                if (cAwake.Held)
                    bodies.Held.push_back(frame.Key);
                // This game moves it (ignored for one a guest moves: its frames play it here).
                if (!cKey->Actor)
                    m_bodyBridge.SetMoving(frame.Key, true);
                bodies.Awake.push_back(frame);
            }
    for (const auto& cRested : cUpdate.Rested)
        if (const auto cKey = cOwner(cRested))
            for (const BodyFrame& cRead : cRested.Read)
            {
                if (!cKey->Actor)
                    m_bodyBridge.SetMoving({cKey->Object, 0, cRead.Key.Index}, false);
                bodies.Rested.push_back({.Key = {cKey->Object, cKey->Actor, cRead.Key.Index}, .Position = cRead.Position, .Rotation = cRead.Rotation, .Knocked = cRead.Knocked});
            }
    return bodies;
}

std::vector<BodyKey> HostEngine::TakeTouches() noexcept
{
    std::vector<BodyKey> keys;
    auto pushes = PlayerHands::Take();
    const auto cHits = PlayerHands::TakeHits();
    pushes.insert(pushes.end(), cHits.begin(), cHits.end());
    for (const auto& cPush : pushes)
    {
        auto* pObject = cPush.Impulse == glm::vec3(0.f) ? nullptr : Cast<TESObjectREFR>(TESForm::GetById(cPush.Object));
        const auto cId = pObject && IsShared(pObject->formID) ? ToSessionOrCreated(pObject->formID) : std::nullopt;
        const auto cIndex = cId ? ReferenceBodies::IndexOf(*pObject, cPush.pBody) : std::nullopt;
        if (cIndex && std::ranges::find(keys, BodyKey{.Object = *cId, .Index = *cIndex}) == keys.end())
            keys.push_back({.Object = *cId, .Index = *cIndex});
    }
    return keys;
}
