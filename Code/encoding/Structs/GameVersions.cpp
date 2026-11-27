#include <Structs/GameVersions.h>

std::string GameVersions::Format(uint32_t aPacked) noexcept
{
    std::string text = std::to_string(aPacked >> 24) + "." + std::to_string((aPacked >> 16) & 0xFF) + "." + std::to_string((aPacked >> 4) & 0xFFF);
    if (aPacked & 0xF)
        text += "." + std::to_string(aPacked & 0xF);
    return text;
}
