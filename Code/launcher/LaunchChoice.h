#pragma once

#include <algorithm>
#include <span>
#include <string_view>

namespace Launcher
{
enum class Choice
{
    Menu,
    Play,
    Graphics
};

// Older bootstrap builds have the same invite-environment marker, even when their bytes differ.
inline bool IsCoopLauncher(std::span<const char> aFile)
{
    constexpr std::u16string_view cMarker = u"SKYRIMCOOP_LAUNCH_ARGS";
    const auto* pFirst = reinterpret_cast<const char*>(cMarker.data());
    return std::search(aFile.begin(), aFile.end(), pFirst, pFirst + cMarker.size() * sizeof(char16_t)) != aFile.end();
}

// Steam invitations always go straight to SKSE, even with a saved launch option.
inline Choice Select(std::span<const std::wstring_view> aArguments)
{
    Choice choice = Choice::Menu;
    for (auto argument : aArguments)
    {
        if (argument == L"+connect_lobby")
            return Choice::Play;
        if (argument == L"--play")
            choice = Choice::Play;
        else if (argument == L"--graphics")
            choice = Choice::Graphics;
    }
    return choice;
}
} // namespace Launcher
