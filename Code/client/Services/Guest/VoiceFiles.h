#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace VoiceFiles
{
// Whether a voice file is a line of the info that owns its audio, in that plugin: the engine names it
// Data\Sound\Voice\<plugin>\<voice type>\<quest>_<topic>_<id %08X>_<response> (25502), the id the info's form id
// masked 0xFFF for a light plugin (its forms load at 0xFE), else 0xFFFFFF.
[[nodiscard]] inline bool IsLineOf(std::string_view aFile, uint32_t aInfo, std::string_view aPlugin) noexcept
{
    if (!aInfo || aPlugin.empty())
        return false;
    const uint32_t cId = aInfo >> 24 == 0xFE ? aInfo & 0xFFF : aInfo & 0xFFFFFF;
    std::string file(aFile), folder = std::format("\\{}\\", aPlugin);
    const auto cLower = [](std::string& aText) { std::ranges::transform(aText, aText.begin(), [](unsigned char c) { return std::tolower(c); }); };
    cLower(file);
    cLower(folder);
    return file.find(folder) != std::string::npos && file.find(std::format("_{:08x}_", cId)) != std::string::npos;
}
} // namespace VoiceFiles
