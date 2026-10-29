#include <Games/Animation/ActionState.h>
#include <catch2/catch.hpp>

#include <atomic>
#include <barrier>
#include <string>
#include <thread>
#include <vector>

namespace
{
ActionEvent MakeAction(uint32_t aId)
{
    ActionEvent action;
    action.ActionId = aId;
    action.Tick = aId;
    action.EventName.assign(64 + aId * 16, static_cast<char>('a' + aId));
    action.TargetEventName.assign(128 + aId, static_cast<char>('A' + aId));
    action.Variables.Integers.assign(32 + aId, aId);
    action.Variables.Floats.assign(48 + aId, static_cast<float>(aId));
    return action;
}

bool Complete(const ActionEvent& acAction)
{
    const uint32_t cId = acAction.ActionId;
    return (cId == 1 || cId == 2) && acAction.Tick == cId &&
           acAction.EventName == std::string(64 + cId * 16, static_cast<char>('a' + cId)) &&
           acAction.TargetEventName == std::string(128 + cId, static_cast<char>('A' + cId)) &&
           acAction.Variables.Integers == std::vector<uint32_t>(32 + cId, cId) &&
           acAction.Variables.Floats == std::vector<float>(48 + cId, static_cast<float>(cId));
}
} // namespace

TEST_CASE("Animation readers keep a complete action while graph and VM writers replace it", "[animation-state]")
{
    AnimationActionState state;
    state.Write(MakeAction(1));
    constexpr int cIterations = 2000;
    std::barrier start(4);
    std::atomic<bool> snapshotsComplete{true};
    const auto cWrite = [&](uint32_t aId) {
        const ActionEvent cAction = MakeAction(aId);
        start.arrive_and_wait();
        for (int i = 0; i < cIterations; ++i)
            state.Write(cAction);
    };
    std::thread graphThread(cWrite, 1);
    std::thread vmThread(cWrite, 2);
    std::thread saveThread([&] {
        start.arrive_and_wait();
        for (int i = 0; i < cIterations; ++i)
            if (!Complete(state.Read()))
                snapshotsComplete = false;
    });
    start.arrive_and_wait();
    graphThread.join();
    vmThread.join();
    saveThread.join();
    REQUIRE(snapshotsComplete.load());
}

TEST_CASE("A refused animation attempt restores only the action it cleared", "[animation-state]")
{
    AnimationActionState state;
    const auto cOriginal = MakeAction(1);
    state.Write(cOriginal);
    const auto cPrevious = state.BeginAttempt();
    REQUIRE(cPrevious.Previous == cOriginal);
    REQUIRE(state.Read().ActionId == 0);

    SECTION("refusal restores the previous action")
    {
        REQUIRE_FALSE(state.FinishAttempt(2, cPrevious));
        REQUIRE(state.Read() == cOriginal);
    }
    SECTION("a body reset is not undone by a refused attempt")
    {
        state.Write({});
        REQUIRE_FALSE(state.FinishAttempt(2, cPrevious));
        REQUIRE(state.Read() == ActionEvent{});
    }
    SECTION("an accepted action remains the latest")
    {
        const auto cAccepted = MakeAction(2);
        state.Write(cAccepted);
        REQUIRE(state.FinishAttempt(2, cPrevious));
        REQUIRE(state.Read() == cAccepted);
    }
    SECTION("another accepted action is not replaced by a refused attempt")
    {
        const auto cOther = MakeAction(3);
        state.Write(cOther);
        REQUIRE_FALSE(state.FinishAttempt(2, cPrevious));
        REQUIRE(state.Read() == cOther);
    }
}
