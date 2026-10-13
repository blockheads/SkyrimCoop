#include <TiltedOnlinePCH.h>
#include <RunningGame.h>

#include <Bridges/IFormBridge.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>

namespace RunningGame
{
static GameVersions s_versions;

void SetVersions(uint32_t aRuntime, uint32_t aSkse)
{
    s_versions = {aRuntime, aSkse};
}

const GameVersions& GetVersions()
{
    return s_versions;
}

Mods Plugins(const IFormBridge& acForms)
{
    Mods plugins;
    for (const auto& cMod : acForms.GetLoadedMods())
    {
        auto& entry = plugins.ModList.emplace_back();
        entry.Id = cMod.Id;
        entry.IsLite = cMod.IsLite;
        entry.Filename = cMod.Filename.c_str();
    }
    return plugins;
}
} // namespace RunningGame
