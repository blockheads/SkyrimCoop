#include "AnimationSystem.h"

#include <spdlog/spdlog.h>
#include <cstring>

#include "../Services/CharacterService.h" // for RemoteComponent, AnimationComponent
#include "../game_bridge/tcp_client.h"
#include "protocol.h"

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

        // Build CMD_PLAY_ANIMATION packet for the DLL
        CommandPacket cmd{};
        cmd.header.opcode = CMD_PLAY_ANIMATION;
        cmd.header.length = sizeof(CommandPacket) - sizeof(PacketHeader);
        cmd.argCount = 3;
        cmd.args[0] = remote.gamePtr;
        cmd.args[1] = anim.lastActionId;
        cmd.args[2] = anim.lastTargetFormId;

        aTcp.Send(&cmd, sizeof(cmd));

        spdlog::debug("AnimationSystem: CMD_PLAY_ANIMATION actor={:#x} action={} target={:#x}",
                       remote.gamePtr, anim.lastActionId, anim.lastTargetFormId);
    }
}
