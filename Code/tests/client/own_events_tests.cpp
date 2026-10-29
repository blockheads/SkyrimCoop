// OwnEvents: which engine events are this game's own player's, what happens to each, and a guest's own quests
// (docs/design/world-state-sync.md, "A guest's own quests"; docs/design/cutscenes.md, "Whose moment").
#include <catch2/catch.hpp>

#include <Papyrus/OwnEvents.h>

using namespace OwnEvents;

namespace
{
constexpr uint64_t kPlayerRef = 0x0000FFFF00000014;      // this game's player, a form
constexpr uint64_t kEffectOnPlayer = 0x0002123400000014; // an active effect on the player
constexpr uint64_t kLever = 0x0000FFFF0001A2B3;          // a reference in the world
constexpr uint64_t kQuest = 0x0000FFFF0003372B;          // a quest, as a form
constexpr uint64_t kAlias = 0x000000050003372B;          // that quest's alias 5
constexpr uint64_t kEffectOnNpc = 0x000212340001A2B3;    // an active effect on an NPC
constexpr uint32_t kQuestId = 0x3372B;

const auto cNoQuest = [](uint32_t) { return false; };
const auto cNoOne = [](uint64_t, std::string_view) { return uint32_t{0}; };

bool Own(uint64_t aHandle, std::string_view aEvent, bool aFromSkse = false)
{
    return IsOwnEvent(SentEvent{aHandle, aEvent, aFromSkse}, cNoQuest, cNoOne);
}
} // namespace

TEST_CASE("What a VM handle names", "[own]")
{
    CHECK(IsAboutPlayer(kPlayerRef));
    CHECK(IsAboutPlayer(kEffectOnPlayer));
    CHECK(!IsAboutPlayer(kLever));
    CHECK(!IsAboutPlayer(kEffectOnNpc));
    CHECK(!IsAboutPlayer(0x0000000500000014)); // alias 5 of a quest 0x14, not the player
    CHECK(QuestOf(kAlias) == kQuestId);
    CHECK(QuestOf(kQuest) == kQuestId);
    CHECK(QuestOf(kEffectOnNpc) == 0);
}

SCENARIO("This game's own events", "[own]")
{
    THEN("the player's own scripts' events are") CHECK(Own(kPlayerRef, "OnHit"));
    THEN("an effect on the player is") CHECK(Own(kEffectOnPlayer, "OnEffectStart"));
    THEN("SKSE's (input, menus, mod events) are") CHECK(Own(kLever, "OnKeyDown", true));
    THEN("the game's load is, any spelling") CHECK(Own(kLever, "onplayerloadgame"));
    THEN("a world reference's is not") CHECK(!Own(kLever, "OnActivate"));
    THEN("an NPC's effect is not") CHECK(!Own(kEffectOnNpc, "OnEffectStart"));

    GIVEN("a quest this guest's own scripts started")
    {
        const auto cOwnQuest = [](uint32_t aQuest) { return aQuest == kQuestId; };
        THEN("its events and its aliases' are")
        {
            CHECK(IsOwnEvent(SentEvent{kQuest, "OnStoryScript"}, cOwnQuest, cNoOne));
            CHECK(IsOwnEvent(SentEvent{kAlias, "OnDeath"}, cOwnQuest, cNoOne));
        }
        THEN("an effect's handle never reads as that quest")
        CHECK(!IsOwnEvent(SentEvent{0x000212340003372B, "OnEffectStart"}, cOwnQuest, cNoOne));
    }

    GIVEN("a timer this game's player's scripts set on a lever")
    {
        const auto cByPlayer = [](uint64_t aHandle, std::string_view aEvent) {
            return aHandle == kLever && aEvent == "OnUpdate" ? kThisPlayer : uint32_t{0};
        };
        THEN("its event is") CHECK(IsOwnEvent(SentEvent{kLever, "OnUpdate"}, cNoQuest, cByPlayer));
        THEN("the lever's other events are not")
        CHECK(!IsOwnEvent(SentEvent{kLever, "OnActivate"}, cNoQuest, cByPlayer));
    }

    GIVEN("a timer a guest's script set (on the host)")
    THEN("not this game's player's")
    CHECK(!IsOwnEvent(SentEvent{kLever, "OnUpdate"}, cNoQuest, [](uint64_t, std::string_view) { return 0xFF000801u; }));
}

SCENARIO("What happens to an event", "[own]")
{
    const auto cOwn = [] { return true; };
    const auto cWorld = [] { return false; };

    GIVEN("the host outside a session")
    THEN("everything runs as sent") CHECK(Deliver(false, false, false, cOwn) == Delivery::kRun);

    GIVEN("the host in a session")
    {
        THEN("its own player's events run as its player")
        CHECK(Deliver(false, true, false, cOwn) == Delivery::kAsThisPlayer);
        THEN("the world's run as sent") CHECK(Deliver(false, true, false, cWorld) == Delivery::kRun);
        THEN("a script's native sending one keeps that script's cause")
        CHECK(Deliver(false, true, true, cOwn) == Delivery::kRun);
    }

    GIVEN("a guest")
    {
        THEN("its own run as its player") CHECK(Deliver(true, true, false, cOwn) == Delivery::kAsThisPlayer);
        THEN("the world's are the host's to run") CHECK(Deliver(true, true, false, cWorld) == Delivery::kDrop);
    }

    GIVEN("the host's world event")
    {
        bool cAsked = false;
        (void)Deliver(false, false, false, [&cAsked] {
            cAsked = true;
            return true;
        });
        THEN("outside a session, whose it is is not even asked") CHECK(!cAsked);
    }
}

SCENARIO("A guest's own quests", "[own]")
{
    OwnQuests quests;
    WHEN("a guest's own script starts a quest (its beast form, its mod's MCM)")
    {
        CHECK(quests.Started(kQuestId, false, true, true, kThisPlayer));
        THEN("it is the guest's") CHECK(quests.Contains(kQuestId));
        AND_WHEN("the guest leaves or joins again")
        {
            quests.Clear();
            THEN("it is no longer") CHECK(!quests.Contains(kQuestId));
        }
    }
    WHEN("the quest was already running")
    THEN("not the guest's") CHECK(!quests.Started(kQuestId, true, true, true, kThisPlayer));
    WHEN("the start failed")
    THEN("not the guest's") CHECK(!quests.Started(kQuestId, false, false, true, kThisPlayer));
    WHEN("the engine or a world script starts it (no player's cause)")
    THEN("not the guest's") CHECK(!quests.Started(kQuestId, false, true, true, 0));
    WHEN("the host's own player's script starts it")
    THEN("the host keeps no own quests") CHECK(!quests.Started(kQuestId, false, true, false, kThisPlayer));
    CHECK(!quests.Contains(0x12345));
}

SCENARIO("The quests one per player", "[own][system-quests]")
{
    constexpr uint32_t kVampireQuest = 0xEAFD5, kPlayerIsVampire = 0xEAFD6, kFeedReady = 0x2D6D1;
    SystemQuestList quests;
    THEN("none at first") CHECK((!quests.Contains(kVampireQuest) && !quests.IsGlobal(kPlayerIsVampire)));
    WHEN("PlayerVampireQuest is learned")
    {
        CHECK(quests.Learn(kVampireQuest));
        THEN("it is one, once") CHECK((quests.Contains(kVampireQuest) && !quests.Learn(kVampireQuest)));
        THEN("its globals are still to read") CHECK(quests.WithoutGlobals() == std::vector<uint32_t>{kVampireQuest});
        AND_WHEN("its scripts' globals are read")
        {
            quests.SetGlobals(kVampireQuest, {kPlayerIsVampire, kFeedReady});
            THEN("they are each game's own") CHECK((quests.IsGlobal(kPlayerIsVampire) && quests.IsGlobal(kFeedReady)));
            THEN("none is left to read") CHECK(quests.WithoutGlobals().empty());
        }
        AND_WHEN("a new game or a load starts")
        {
            quests.Clear();
            THEN("it is no longer") CHECK((!quests.Contains(kVampireQuest) && quests.All().empty()));
        }
    }
    THEN("globals of a quest not learned are not kept")
    {
        quests.SetGlobals(kVampireQuest, {kPlayerIsVampire});
        CHECK(!quests.IsGlobal(kPlayerIsVampire));
    }
}
