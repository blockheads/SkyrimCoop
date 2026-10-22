#include <World.h>
#include <Components.h>
#include <Actor.h>

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
        spdlog::debug("{}: This entity has neither a local or remote component: {:X}, form id: {:X}", __FUNCTION__, to_integral(aEntity), pFormIdComponent ? pFormIdComponent->Id : 0);
        return std::nullopt;
    }

    return {sessionId};
}

void ShowHudMessage(const std::string& acMessage)
{
    using TShowHudMessage = void(const char*, const char*, bool);

    POINTER_SKYRIMSE(TShowHudMessage, s_showHudMessage, 52933);

    s_showHudMessage(acMessage.c_str(), nullptr, false);
}

} // namespace Utils
