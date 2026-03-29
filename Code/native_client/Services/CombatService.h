#pragma once
#include <cstdint>

#include <nlohmann/json_fwd.hpp>

class GameReader;
class TcpClient;

// CombatService: Handles combat-related hook events (damage, projectiles)
// and syncs combat state over the network.
// Receives: HOOK_DAMAGE_ACTOR, HOOK_PROJECTILE
class CombatService {
public:
    CombatService(GameReader& aReader, TcpClient& aClient);

    // Dispatch hook events from DLL
    void OnHookEvent(const nlohmann::json& aMsg);

    // Periodic update
    void Update(float aDeltaTime);

private:
    GameReader& m_reader;
    TcpClient& m_client;
    uint32_t m_damageEventCount = 0;
    uint32_t m_projectileEventCount = 0;
};
