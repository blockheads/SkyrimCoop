// GuestScripts' tags: which player each call, stack, timer and delayed native runs for (docs/design/cutscenes.md,
// "Whose moment"; docs/re/systems/papyrus-guest-scripts.md).
#include <catch2/catch.hpp>

#include <Papyrus/ScriptTags.h>

using namespace GuestScripts;

namespace
{
constexpr uint32_t kGuest = 0xFF000801; // a guest's character on the host
constexpr uint32_t kOtherGuest = 0xFF000802;
constexpr uint64_t kScript = 0x0000FFFF00012345; // a script object's handle
constexpr uint64_t kQuest = 0x000000000003372B;  // a quest's script

const auto cNoGuest = [] { return uint32_t{0}; };
const auto cNoPlayer = [] { return false; };

// A call queued by the engine (no native running), in a session.
QueuedCall EngineEvent(std::string_view aEvent, uint64_t aSelf = kScript)
{
    return {.InSession = true, .Self = aSelf, .Event = aEvent};
}

// A call a native queues in a stack running for acCause.
QueuedCall FromNative(const Cause& acCause, std::string_view aEvent = "SomeFunction")
{
    return {.Native = acCause, .NativeDepth = 1, .InSession = true, .Self = kScript, .Event = aEvent};
}

// The cause aCall gets, through a stack, as the VM runs it: queued, given a stack, read back.
Cause RunsFor(Tags& aTags, const Cause& acCause, uint32_t aStackId, const void* apCall)
{
    aTags.TagCall(apCall, acCause);
    aTags.MoveToStack(apCall, aStackId);
    return aTags.OfStack(aStackId);
}
} // namespace

SCENARIO("A queued call's cause", "[tags]")
{
    Tags tags;

    GIVEN("the engine's event of a guest's action (a guest's character among its arguments)")
    THEN("it runs for that guest")
    CHECK(CauseOfCall(tags, EngineEvent("OnActivate"), [] { return kGuest; }, cNoPlayer) == Cause{kGuest, false});

    GIVEN("the engine's event of this game's player's action, in a session")
    THEN("it runs for this game's player")
    CHECK(CauseOfCall(tags, EngineEvent("OnActivate"), cNoGuest, [] { return true; }) == Cause{kThisPlayer, false});

    GIVEN("this game's player as an argument, but outside a session")
    {
        QueuedCall call = EngineEvent("OnActivate");
        call.InSession = false;
        THEN("no one's") CHECK(CauseOfCall(tags, call, cNoGuest, [] { return true; }) == Cause{});
    }

    GIVEN("this game's player as an argument of a call a native queues (a script's call, not the player's action)")
    THEN("no one's")
    CHECK(CauseOfCall(tags, FromNative({}), cNoGuest, [] { return true; }) == Cause{});

    GIVEN("a native running for a guest queues it, with another guest among its arguments")
    THEN("it runs for the native's guest")
    CHECK(
        CauseOfCall(tags, FromNative({kGuest, false}), [] { return kOtherGuest; }, cNoPlayer) == Cause{kGuest, false});

    GIVEN("a native running for a player's story queues it (a stage fragment)")
    THEN("it is the story too")
    CHECK(CauseOfCall(tags, FromNative({kThisPlayer, true}), cNoGuest, cNoPlayer) == Cause{kThisPlayer, true});

    GIVEN("a native with no player queues it")
    THEN("the story mark does not survive without a player")
    CHECK(CauseOfCall(tags, FromNative({0, true}), cNoGuest, cNoPlayer) == Cause{});

    GIVEN("a native's guest")
    {
        bool cRead = false;
        const auto cReads = [&cRead] {
            cRead = true;
            return kOtherGuest;
        };
        (void)CauseOfCall(tags, FromNative({kGuest, false}), cReads, cNoPlayer);
        THEN("the arguments are not read") CHECK(!cRead);
    }
}

SCENARIO("A stack keeps its call's cause until it ends", "[tags]")
{
    Tags tags;
    int call = 0;

    CHECK(!tags.Any());
    CHECK(RunsFor(tags, {kGuest, false}, 7, &call) == Cause{kGuest, false});
    CHECK(tags.Any());

    WHEN("the stack ends")
    {
        tags.EndStack(7);
        THEN("its id runs for no one") CHECK(tags.OfStack(7) == Cause{});
    }

    WHEN("a new call is queued at the old call's address with no cause")
    {
        int reused = 0;
        tags.TagCall(&reused, {kGuest, false});
        tags.TagCall(&reused, {});
        tags.MoveToStack(&reused, 8);
        THEN("its stack runs for no one") CHECK(tags.OfStack(8) == Cause{});
    }

    WHEN("a stack is made for a call never tagged")
    {
        int untagged = 0;
        tags.MoveToStack(&untagged, 9);
        THEN("it runs for no one") CHECK(tags.OfStack(9) == Cause{});
    }
}

SCENARIO("A timer or registration answers to whoever set it", "[tags]")
{
    Tags tags;

    GIVEN("a guest's script registers for a single update")
    {
        NativeCalled(tags, "RegisterForSingleUpdate", kScript, {kGuest, false});
        THEN("RegisteredBy names the guest, any spelling, without using it up")
        {
            CHECK(tags.RegisteredBy(kScript, "onupdate") == kGuest);
            CHECK(tags.RegisteredBy(kScript, "OnUpdate") == kGuest);
        }
        WHEN("OnUpdate fires")
        {
            const Cause cFirst = CauseOfCall(tags, EngineEvent("OnUpdate"), cNoGuest, cNoPlayer);
            const Cause cSecond = CauseOfCall(tags, EngineEvent("OnUpdate"), cNoGuest, cNoPlayer);
            THEN("it runs for the guest, once") CHECK(cFirst == Cause{kGuest, false});
            THEN("a second update is no one's") CHECK(cSecond == Cause{});
        }
        WHEN("another script object's OnUpdate fires")
        THEN("not the guest's")
        CHECK(CauseOfCall(tags, EngineEvent("OnUpdate", kQuest), cNoGuest, cNoPlayer) == Cause{});
    }

    GIVEN("a repeating registration (RegisterForUpdate)")
    {
        NativeCalled(tags, "RegisterForUpdate", kScript, {kGuest, false});
        THEN("every update runs for the guest")
        {
            CHECK(CauseOfCall(tags, EngineEvent("OnUpdate"), cNoGuest, cNoPlayer).Player == kGuest);
            CHECK(CauseOfCall(tags, EngineEvent("OnUpdate"), cNoGuest, cNoPlayer).Player == kGuest);
        }
        WHEN("it is unregistered")
        {
            NativeCalled(tags, "UnregisterForUpdate", kScript, {kGuest, false});
            THEN("updates are no one's")
            CHECK(CauseOfCall(tags, EngineEvent("OnUpdate"), cNoGuest, cNoPlayer) == Cause{});
        }
        WHEN("a script with no player registers again")
        {
            NativeCalled(tags, "RegisterForUpdate", kScript, {});
            THEN("updates are no one's")
            CHECK(CauseOfCall(tags, EngineEvent("OnUpdate"), cNoGuest, cNoPlayer) == Cause{});
        }
    }

    GIVEN("a registration answering two events (RegisterForAnimationEvent)")
    {
        NativeCalled(tags, "RegisterForAnimationEvent", kScript, {kThisPlayer, true});
        THEN("both events run for its player, story kept")
        {
            CHECK(CauseOfCall(tags, EngineEvent("OnAnimationEvent"), cNoGuest, cNoPlayer) == Cause{kThisPlayer, true});
            CHECK(CauseOfCall(tags, EngineEvent("OnAnimationEventUnregistered"), cNoGuest, cNoPlayer) ==
                  Cause{kThisPlayer, true});
        }
    }

    GIVEN("a native that sets no timer")
    {
        NativeCalled(tags, "SetStage", kQuest, {kGuest, false});
        THEN("nothing is kept") CHECK(!tags.HasTimers());
    }

    GIVEN("a timer set, and an event that names a guest's character")
    {
        NativeCalled(tags, "RegisterForSingleUpdate", kScript, {kGuest, false});
        THEN("the argument wins and the timer stays")
        {
            CHECK(CauseOfCall(
                      tags, EngineEvent("OnUpdate"), [] { return kOtherGuest; }, cNoPlayer)
                      .Player == kOtherGuest);
            CHECK(tags.RegisteredBy(kScript, "OnUpdate") == kGuest);
        }
    }
}

SCENARIO("The story follows a stage a player's scripts set", "[tags]")
{
    THEN("a native setting a stage in a player's stack makes what it queues the story")
    CHECK(Within({kThisPlayer, false}, true) == Cause{kThisPlayer, true});
    THEN("other natives in a player's stack stay the player's moment")
    CHECK(Within({kGuest, false}, false) == Cause{kGuest, false});
    THEN("the story stays the story") CHECK(Within({kGuest, true}, false) == Cause{kGuest, true});
    THEN("no player's stack has no story") CHECK(Within({}, true) == Cause{});
}

SCENARIO("A guest's lever to a cutscene: the chain from action to story", "[tags]")
{
    Tags tags;
    int activate = 0, update = 0, fragment = 0;

    // The guest pulls a lever: its OnActivate names the guest's character.
    const Cause cActivate = CauseOfCall(tags, EngineEvent("OnActivate"), [] { return kGuest; }, cNoPlayer);
    REQUIRE(RunsFor(tags, cActivate, 1, &activate) == Cause{kGuest, false});
    // Its script waits a second: RegisterForSingleUpdate in that stack.
    NativeCalled(tags, "RegisterForSingleUpdate", kScript, tags.OfStack(1));
    tags.EndStack(1);

    // The update fires, runs for the guest; it sets the quest's stage.
    const Cause cUpdate = CauseOfCall(tags, EngineEvent("OnUpdate"), cNoGuest, cNoPlayer);
    REQUIRE(RunsFor(tags, cUpdate, 2, &update) == Cause{kGuest, false});
    const Cause cSetStage = Within(tags.OfStack(2), true);

    // The stage's fragment, queued by SetStage: the story.
    const Cause cFragment = CauseOfCall(tags, FromNative(cSetStage, "Fragment_10"), cNoGuest, cNoPlayer);
    CHECK(RunsFor(tags, cFragment, 3, &fragment) == Cause{kGuest, true});
}

SCENARIO("A delayed native runs for the guest whose script queued it", "[tags]")
{
    Tags tags;
    int cast = 0;
    tags.TagFunctor(&cast, kGuest);
    CHECK(tags.TakeFunctor(&cast) == kGuest);
    CHECK(tags.TakeFunctor(&cast) == 0);
}

TEST_CASE("Timer natives and their events, any spelling", "[tags]")
{
    REQUIRE(FindTimerNative("registerforsingleupdate"));
    CHECK(FindTimerNative("RegisterForSingleUpdate")->Kind == Timer::kOnce);
    CHECK(FindTimerNative("UnregisterForLOS")->Kind == Timer::kStop);
    CHECK(!FindTimerNative("RegisterForSingleUpdat"));
    CHECK(TimerEvent("ONUPDATEGAMETIME") == "OnUpdateGameTime");
    CHECK(TimerEvent("OnActivate").empty());
}
