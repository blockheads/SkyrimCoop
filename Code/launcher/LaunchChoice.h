#pragma once

#include <span>
#include <string_view>

namespace Launcher
{
enum class Choice { Menu, Play, Graphics };

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
