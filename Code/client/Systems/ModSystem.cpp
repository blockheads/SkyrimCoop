#include <Systems/ModSystem.h>

#include <Structs/Mods.h>
#include <Structs/GameId.h>

#include <Bridges/IFormBridge.h>
#include <Games/EngineNames.h>

#include <cstring>
#include <limits>

ModSystem::ModSystem(entt::dispatcher& aDispatcher, const IFormBridge& acForms) noexcept
    : m_forms(acForms)
{
    std::memset(m_standardToServer, 0, sizeof(m_standardToServer));
    // Deal with temporary ids
    m_standardToServer[0xFF] = std::numeric_limits<uint32_t>::max();

    m_modsConnection = aDispatcher.sink<Mods>().connect<&ModSystem::HandleMods>(this);
}

bool ModSystem::GetServerModId(const uint32_t aGameId, uint32_t& aModId, uint32_t& aBaseId) const noexcept
{
    uint32_t hiByte = aGameId >> 24;

    // If it's a lite mod
    if (hiByte == 0xFE)
    {
        const uint16_t liteId = ((aGameId & 0x00FFF000) >> 12) & 0xFFFF;

        const auto itor = m_liteToServer.find(liteId);
        if (itor != std::end(m_liteToServer))
        {
            aModId = itor->second;
            aBaseId = aGameId & 0x00000FFF;
            return true;
        }

        return false;
    }

    // Here we have a standard mod
    aModId = m_standardToServer[hiByte & 0xFF];
    aBaseId = aGameId & 0x00FFFFFFu;

    return true;
}

bool ModSystem::GetServerModId(uint32_t aGameId, GameId& aSessionId) const noexcept
{
    return GetServerModId(aGameId, aSessionId.ModId, aSessionId.BaseId);
}

uint32_t ModSystem::GetGameId(uint32_t aSessionId, uint32_t aFormId) const noexcept
{
    auto itor = m_serverToGame.find(aSessionId);
    if (itor != std::end(m_serverToGame))
    {
        if (itor->second.isLite)
        {
            aFormId &= 0xFFFu;
            aFormId |= 0xFE000000u;
            aFormId |= uint32_t(itor->second.id) << 12;
        }
        else
        {
            aFormId &= 0x00FFFFFFu;
            aFormId |= uint32_t(itor->second.id) << 24;
        }

        return aFormId;
    }

    return 0;
}

uint32_t ModSystem::GetGameId(const GameId& acGameId) const noexcept
{
    return GetGameId(acGameId.ModId, acGameId.BaseId);
}

void ModSystem::HandleMods(const Mods& acMods) noexcept
{
    m_serverToGame.clear();
    m_liteToServer.clear();
    std::memset(m_standardToServer, 0, sizeof(m_standardToServer));
    m_standardToServer[0xFF] = std::numeric_limits<uint32_t>::max();

    const auto cLoaded = m_forms.GetLoadedMods();

    for (const auto& mod : acMods.ModList)
    {
        const auto cHere = std::ranges::find_if(cLoaded, [&mod](const IFormBridge::ModEntry& acEntry) {
            return SameName(acEntry.Filename, mod.Filename.c_str());
        });
        if (cHere != cLoaded.end())
        {
            if (mod.IsLite)
            {
                m_serverToGame.emplace(mod.Id, GameMod{static_cast<uint16_t>(cHere->Id & 0xFFFu), true});
                m_liteToServer.emplace(cHere->Id, mod.Id);
            }
            else
            {
                m_serverToGame.emplace(mod.Id, GameMod{static_cast<uint16_t>(cHere->Id & 0xFFu), false});
                m_standardToServer[cHere->Id & 0xFF] = mod.Id;
            }
        }
        else
        {
            spdlog::error("Failed to find mod {}, is lite? {}, id: {:X}", mod.Filename.c_str(), mod.IsLite, mod.Id);
        }
    }
}
