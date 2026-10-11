#pragma once

#include <optional>
#include <string>

namespace CoopReload
{
// State this client hands to the next one across a hot reload, kept by the loader
// (Code/hotreload/Loader.cpp): what the engine told us once at boot and will not tell again.
void HandOff(const char* acpKey, const std::string& acData);
// What the previous client handed off under acpKey; nothing when not hot reloaded.
std::optional<std::string> TakeHandoff(const char* acpKey);
} // namespace CoopReload
