#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>

namespace TestHarness
{
// Game-thread-only development session controller. Owns no engine hooks.
nlohmann::json PollSession(const std::filesystem::path& directory);
}
