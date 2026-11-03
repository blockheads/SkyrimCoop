#include "../../launcher/LaunchChoice.h"
#include <array>
#include <catch2/catch.hpp>
#include <string>

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

TEST_CASE("An older co-op bootstrap is rejected as a preserved Bethesda launcher", "[launcher]")
{
    std::string oldBuild(2048, 'x');
    const std::u16string_view marker = u"SKYRIMCOOP_LAUNCH_ARGS";
    oldBuild.append(reinterpret_cast<const char*>(marker.data()), marker.size() * sizeof(char16_t));
    oldBuild += "old executable bytes";
    CHECK(Launcher::IsCoopLauncher(oldBuild));
    oldBuild[100] = 'y';
    CHECK(Launcher::IsCoopLauncher(oldBuild));
    CHECK_FALSE(Launcher::IsCoopLauncher({}));
    CHECK_FALSE(Launcher::IsCoopLauncher(std::string_view("an original Bethesda launcher")));
}
