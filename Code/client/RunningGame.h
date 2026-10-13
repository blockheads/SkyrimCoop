#pragma once

#include <cstdint>

struct GameVersions;
struct Mods;
class IFormBridge;

// What this game runs: the Skyrim runtime and SKSE (as SKSE reports them at load), and its plugins.
namespace RunningGame
{
// Once, as the plugin loads.
void SetVersions(uint32_t aRuntime, uint32_t aSkse);
const GameVersions& GetVersions();
// The loaded plugins in load order, as the join request carries them.
Mods Plugins(const IFormBridge& acForms);
} // namespace RunningGame
