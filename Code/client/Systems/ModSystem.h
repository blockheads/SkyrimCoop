#pragma once

#include <NoCopyMove.h>

struct Mods;
struct GameId;
class IFormBridge;

/**
 * @brief Makes sure that different clients can have independent load orders and mods.
 */
struct ModSystem
{
    // aForms: this game's loaded plugins, which the session's mod ids map to.
    ModSystem(entt::dispatcher& aDispatcher, const IFormBridge& acForms) noexcept;
    ~ModSystem() = default;

    NO_COPY_MOVE(ModSystem);

    bool GetServerModId(uint32_t aGameId, uint32_t& aModId, uint32_t& aBaseId) const noexcept;
    bool GetServerModId(uint32_t aGameId, GameId& aSessionId) const noexcept;
    uint32_t GetGameId(uint32_t aSessionId, uint32_t aFormId) const noexcept;
    uint32_t GetGameId(const GameId& acGameId) const noexcept;

protected:
    void HandleMods(const Mods& acMods) noexcept;

private:
    struct GameMod
    {
        uint16_t id;
        bool isLite;
    };

    const IFormBridge& m_forms;
    entt::scoped_connection m_modsConnection;
    std::unordered_map<uint16_t, uint32_t> m_liteToServer;
    std::unordered_map<uint32_t, GameMod> m_serverToGame;
    uint32_t m_standardToServer[0x100];
};
