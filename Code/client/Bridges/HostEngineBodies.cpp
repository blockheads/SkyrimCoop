// HostEngine's half for the host's bodies (docs/design/physics-sync.md): read for the guests.
#include <Bridges/HostEngine.h>

#include <FormIds.h>
#include <TESObjectREFR.h>

#include <RTTI.h>

HostBodies HostEngine::TakeBodies(const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept
{
    HostBodies bodies;
    const auto cOwner = [&](const PhysicsBodies::Bodies& acBodies) -> std::optional<BodyKey> {
        if (acBodies.Actor)
        {
            const uint32_t cSessionId = aSessionOf ? aSessionOf(acBodies.FormId) : 0;
            return cSessionId ? std::optional<BodyKey>(BodyKey{.Actor = cSessionId}) : std::nullopt;
        }
        const auto cId = IsShared(acBodies.FormId) ? ToSessionOrCreated(acBodies.FormId) : std::nullopt;
        return cId ? std::optional<BodyKey>(BodyKey{.Object = *cId}) : std::nullopt;
    };
    const auto cUpdate = m_bodies.Take();
    for (const auto& cAwake : cUpdate.Awake)
        if (const auto cKey = cOwner(cAwake))
            for (BodyFrame frame : cAwake.Read)
            {
                frame.Key = {cKey->Object, cKey->Actor, frame.Key.Index};
                bodies.Awake.push_back(frame);
            }
    for (const auto& cRested : cUpdate.Rested)
        if (const auto cKey = cOwner(cRested))
            for (const BodyFrame& cRead : cRested.Read)
                bodies.Rested.push_back({.Key = {cKey->Object, cKey->Actor, cRead.Key.Index}, .Position = cRead.Position, .Rotation = cRead.Rotation, .Knocked = cRead.Knocked});
    return bodies;
}
