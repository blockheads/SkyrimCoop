#include "Party.h"

using namespace GameTest;
namespace
{
void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = 0x30A4F;
    aEngine.LoadCell(0x30A4F);
}
} // namespace

SCENARIO("A quest reward reaches all four players as input, with each game's own multiplier once", "[rewards]")
{
    Party party(3, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& first = Party::Engine(party.Guest(0));
    auto& second = Party::Engine(party.Guest(1));
    auto& third = Party::Engine(party.Guest(2));
    host.RewardMultiplier = 2.f;
    first.RewardMultiplier = 3.f;
    second.RewardMultiplier = 4.f;
    third.RewardMultiplier = 0.5f;
    const bool cGuest = GENERATE(false, true);
    uint32_t picker = FakeEngine::kPlayer;
    if (cGuest)
    {
        party.Until(
            [&] {
                for (const auto id : host.CopiesOf())
                    if (host.Actors.at(id).Player)
                    {
                        picker = id;
                        return true;
                    }
                return false;
            },
            "the host has the guest's character");
    }
    WHEN("the picker turns in a quest: the host's fragment grants AdvanceSkill's base amount")
    {
        party.As(party.Host(), [&] {
            host.RunScript(
                {.Class = "Game", .Function = "AdvanceSkill", .Arguments = {std::string("Alteration"), 10.f}}, picker,
                true);
        });
        party.Until(
            [&] {
                return third.RewardCalls.size() == 1 && first.RewardCalls.size() == 1 && second.RewardCalls.size() == 1;
            },
            "every guest's own engine runs the reward");
        for (int i = 0; i < 10; ++i)
            party.Frame();
        CHECK(host.RewardExperience[18] == 20.f);
        CHECK(first.RewardExperience[18] == 30.f);
        CHECK(second.RewardExperience[18] == 40.f);
        CHECK(third.RewardExperience[18] == 5.f);
        for (auto* engine : {&host, &first, &second, &third})
        {
            REQUIRE(engine->RewardCalls.size() == 1);
            CHECK(engine->RewardCalls[0].Arguments[1] == LocalValue{10.f});
        }
    }
}

SCENARIO("Direct dialogue and book rewards share levels, perks, points and permanent stats once", "[rewards]")
{
    Party party(3, Plugins);
    auto& host = Party::Engine(party.Host());
    WHEN("Amren grants skill levels before SetStage, or the Oghma grants five directly from its book")
    {
        const int32_t count = GENERATE(1, 5);
        party.As(party.Host(), [&] {
            host.RunScript(
                {.Class = "Game", .Function = "IncrementSkillBy", .Arguments = {std::string("OneHanded"), count}}, 0x14,
                false);
            host.RunScript({.Class = "Game", .Function = "AddPerkPoints", .Arguments = {int32_t{2}}}, 0x14, false);
            host.RunScript(
                {.Class = "Actor", .Function = "AddPerk", .Self = LocalForm{0x14}, .Arguments = {LocalForm{0xABC}}},
                0x14, false);
            host.RunScript({.Class = "Actor",
                            .Function = "ModActorValue",
                            .Self = LocalForm{0x14},
                            .Arguments = {std::string("CarryWeight"), 25.f}},
                           0x14, true);
        });
        party.Until(
            [&] {
                return host.RewardCalls.size() == 4 && Party::Engine(party.Guest(0)).RewardCalls.size() == 4 &&
                       Party::Engine(party.Guest(1)).RewardCalls.size() == 4 &&
                       Party::Engine(party.Guest(2)).RewardCalls.size() == 4;
            },
            "all four rewards arrive");
        for (int i = 0; i < 10; ++i)
            party.Frame();
        for (World* game : {&party.Host(), &party.Guest(0), &party.Guest(1), &party.Guest(2)})
        {
            auto& engine = Party::Engine(*game);
            CHECK(engine.RewardLevels[6] == count);
            CHECK(engine.RewardPoints == 2);
            CHECK(engine.RewardPerks.contains(0xABC));
            CHECK(engine.RewardStats[32] == 25.f);
            CHECK(engine.RewardCalls.size() == 4);
        }
        party.As(party.Host(), [&] {
            host.RunScript(
                {.Class = "Actor", .Function = "RemovePerk", .Self = LocalForm{0x14}, .Arguments = {LocalForm{0xABC}}},
                0x14, false);
        });
        party.Until(
            [&] {
                return !Party::Engine(party.Guest(0)).RewardPerks.contains(0xABC) &&
                       !Party::Engine(party.Guest(1)).RewardPerks.contains(0xABC) &&
                       !Party::Engine(party.Guest(2)).RewardPerks.contains(0xABC);
            },
            "a changed Black Book choice removes the old perk too");
        CHECK_FALSE(host.RewardPerks.contains(0xABC));
    }
}

SCENARIO("A picked line's Speech and a body's buff do not reach the party", "[rewards]")
{
    Party party(1, Plugins);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.As(party.Host(), [&] {
        host.RunScript({.Class = "Game", .Function = "AdvanceSkill", .Arguments = {std::string("Speechcraft"), 10.f}},
                       0x14, false);
        host.RunScript({.Class = "Actor",
                        .Function = "ModActorValue",
                        .Self = LocalForm{0x14},
                        .Arguments = {std::string("Health"), 10.f}},
                       0x14, true, true); // a body's: an effect's permanent-underneath ModAV
    });
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(host.RewardExperience[17] == 10.f);
    CHECK(guest.RewardCalls.empty());
}
