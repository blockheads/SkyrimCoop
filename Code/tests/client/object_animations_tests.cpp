// The host's scripts' animations on a guest's object (docs/design/traps.md): in order, each once its graph takes it.
#include <catch2/catch.hpp>

#include <ObjectAnimations.h>

#include <vector>

TEST_CASE("An animation its graph refuses waits for it, and the next stays behind it", "[ObjectAnimations]")
{
    ObjectAnimations<int, std::pair<int, std::string>> animations;
    const std::chrono::steady_clock::time_point cStart{};
    bool busy = false;
    std::vector<std::pair<int, std::string>> played;
    const auto cSend = [&](const std::pair<int, std::string>& acEvent) {
        if (busy && acEvent.first == 1)
            return false;
        played.push_back(acEvent);
        return true;
    };
    animations.Play(1, {1, "180For"}, cStart, cSend);
    busy = true; // its clip still runs here
    animations.Play(1, {1, "180Back"}, cStart, cSend);
    animations.Play(1, {1, "Rearm"}, cStart, cSend);
    animations.Play(2, {2, "Single"}, cStart, cSend); // another object's
    CHECK(played == std::vector<std::pair<int, std::string>>{{1, "180For"}, {2, "Single"}});
    busy = false;
    animations.Update(cStart + std::chrono::milliseconds(200), cSend);
    CHECK(played.size() == 4);
    CHECK(played[2].second == "180Back");
    CHECK(played[3].second == "Rearm");
    CHECK(animations.Empty());
}

TEST_CASE("An animation its graph never takes is given up", "[ObjectAnimations]")
{
    ObjectAnimations<int, std::pair<int, std::string>> animations;
    const std::chrono::steady_clock::time_point cStart{};
    const auto cNever = [](const std::pair<int, std::string>& acEvent) { return acEvent.second == "Reset"; };
    animations.Play(1, {1, "NotHere"}, cStart, cNever);
    animations.Play(1, {1, "Reset"}, cStart + std::chrono::seconds(1), cNever);
    CHECK_FALSE(animations.Empty());
    animations.Update(cStart + std::chrono::seconds(3), cNever);
    CHECK(animations.Empty());
}
