#pragma once

#include <optional>
#include <string>

namespace CoopReload
{
#if COOP_TEST_HARNESS
// State this client hands to the next one across a hot reload, kept by the loader
// (Code/hotreload/Loader.cpp): what the engine told us once at boot and will not tell again.
void HandOff(const char* acpKey, const std::string& acData);
// What the previous client handed off under acpKey; nothing when not hot reloaded.
std::optional<std::string> TakeHandoff(const char* acpKey);
#else
// Builds without the test harness have no hot reload: nothing is handed on.
inline void HandOff(const char*, const std::string&)
{
}
inline std::optional<std::string> TakeHandoff(const char*)
{
    return std::nullopt;
}
#endif
} // namespace CoopReload
