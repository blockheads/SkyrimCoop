#include <catch2/catch.hpp>
#include "../../launcher/LaunchChoice.h"
#include <array>

TEST_CASE("Steam invitations skip the launcher menu and override graphics options", "[launcher]")
{
    using namespace Launcher;
    CHECK(Select({}) == Choice::Menu);
    CHECK(Select(std::array<std::wstring_view, 1>{L"--play"}) == Choice::Play);
    CHECK(Select(std::array<std::wstring_view, 1>{L"--graphics"}) == Choice::Graphics);
    CHECK(Select(std::array<std::wstring_view, 3>{L"--graphics", L"+connect_lobby", L"123456"}) == Choice::Play);
    CHECK(Select(std::array<std::wstring_view, 2>{L"+connect_lobby", L"--graphics"}) == Choice::Play);
    CHECK(Select(std::array<std::wstring_view, 1>{L"name+connect_lobby"}) == Choice::Menu);
}
