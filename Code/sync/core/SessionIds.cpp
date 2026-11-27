#include <core/SessionIds.h>

#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <core/World.h>

namespace Utils
{
std::optional<uint32_t> GetSessionId(entt::entity aEntity) noexcept
{
    const auto* pLocalComponent = World::Get().try_get<LocalComponent>(aEntity);
    const auto* pRemoteComponent = World::Get().try_get<RemoteComponent>(aEntity);

    uint32_t sessionId = -1;
    if (pLocalComponent)
        sessionId = pLocalComponent->Id;
    else if (pRemoteComponent)
        sessionId = pRemoteComponent->Id;
    else
    {
        const auto* pFormIdComponent = World::Get().try_get<FormIdComponent>(aEntity);
        spdlog::debug("{}: This entity has neither a local or remote component: {:X}, form id: {:X}", __FUNCTION__,
                      to_integral(aEntity), pFormIdComponent ? pFormIdComponent->Id : 0);
        return std::nullopt;
    }

    return {sessionId};
}

uint32_t GetFormIdBySessionId(uint32_t aSessionId) noexcept
{
    // A held copy has neither Local nor Remote: no session id.
    const auto cEntity = World::Get().GetActorIndex().BySession<FormIdComponent>(aSessionId);
    if (cEntity != entt::null)
        return World::Get().get<FormIdComponent>(cEntity).Id;
    spdlog::debug("GetFormIdBySessionId: form not found for session id {:X}", aSessionId);
    return 0;
}

std::optional<uint32_t> GetSessionIdByFormId(uint32_t aFormId) noexcept
{
    if (!aFormId)
        return std::nullopt;
    const auto cEntity = World::Get().GetActorIndex().ByForm(aFormId);
    if (cEntity == entt::null)
        return std::nullopt;
    return GetSessionId(cEntity);
}
} // namespace Utils
