#pragma once

#include <filesystem>
#include <string>

// Where the plugin keeps its files: Data/SKSE/Plugins, also when the hot reload loader runs the
// client from a copy elsewhere.
namespace PluginPaths
{
// Once, as the plugin loads.
void SetDirectory(const std::filesystem::path& acDirectory);
const std::filesystem::path& GetDirectory();
// "-<name>" from SKYRIMCOOP_INSTANCE, or empty: keeps two games in one install apart (logs, control files).
std::string InstanceSuffix();
} // namespace PluginPaths
