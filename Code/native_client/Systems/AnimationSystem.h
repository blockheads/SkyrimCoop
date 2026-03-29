#pragma once
#include <entt/entt.hpp>

class TcpClient;

// AnimationSystem: Applies animation variable updates for remote actors.
// When a remote actor has a dirty AnimationComponent, sends cmd_play_animation JSON
// to the DLL relay for game-thread application.
class AnimationSystem {
public:
    // Process dirty animation components and send commands to DLL
    static void Update(entt::registry& aRegistry, TcpClient& aTcp, float aDeltaTime);
};
