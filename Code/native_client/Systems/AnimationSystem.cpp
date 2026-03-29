#include "AnimationSystem.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "../Services/CharacterService.h" // for RemoteComponent, AnimationComponent
#include "../game_bridge/tcp_client.h"
#include "json_protocol.h"

void AnimationSystem::Update(entt::registry& aRegistry, TcpClient& aTcp, float /*aDeltaTime*/)
{
    auto view = aRegistry.view<RemoteComponent, AnimationComponent>();

    for (auto entity : view) {
        auto& anim = view.get<AnimationComponent>(entity);

        if (!anim.dirty)
            continue;

        anim.dirty = false;

        auto& remote = view.get<RemoteComponent>(entity);
        if (remote.gamePtr == 0)
            continue;

        // Build cmd_play_animation JSON for the DLL
        nlohmann::json cmd = {
            {JsonField::kOp, JsonOp::kCmdPlayAnimation},
            {JsonField::kActorPtr, remote.gamePtr},
            {JsonField::kActionId, anim.lastActionId},
            {JsonField::kTargetFormId, anim.lastTargetFormId}
        };
        aTcp.SendJsonLine(cmd);

        spdlog::debug("AnimationSystem: cmd_play_animation actor={:#x} action={} target={:#x}",
                       remote.gamePtr, anim.lastActionId, anim.lastTargetFormId);
    }
}
