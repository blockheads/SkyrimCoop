#pragma once

#include <TiltedCore/Stl.hpp>

#include <cctype>
#include <cstring>
#include <string_view>

// Engine names (BSFixedString) are pooled case-insensitively and read back as first pooled ("getplayer"):
// compare and key them with these (docs/re/systems/engine-names.md).
[[nodiscard]] inline bool SameName(std::string_view aName, std::string_view aOther) noexcept
{
    return aName.size() == aOther.size() && _strnicmp(aName.data(), aOther.data(), aName.size()) == 0;
}

// A map key for an engine name: the same for every spelling of it.
[[nodiscard]] inline std::string NameKey(std::string_view aName)
{
    std::string key(aName);
    for (char& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}
