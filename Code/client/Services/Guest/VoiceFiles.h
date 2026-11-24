#pragma once

#include <cstdint>
#include <format>
#include <string_view>

namespace VoiceFiles
{
// Whether a voice file is a line of that topic info: the Creation Kit names it
// <quest>_<topic>_<info's id, its plugin index as 00>_<response>.fuz (Skyrim.esm's Andurs: ..._000D79A5_1.wav).
[[nodiscard]] inline bool IsLineOf(std::string_view aFile, uint32_t aInfo) noexcept
{
    return aInfo && aFile.find(std::format("_00{:06X}_", aInfo & 0xFFFFFF)) != std::string_view::npos;
}
} // namespace VoiceFiles
