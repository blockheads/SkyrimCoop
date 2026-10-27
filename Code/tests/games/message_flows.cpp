// The host's messages on every game (docs/design/party-model.md, rule 5; party-calls.md): a story's notification or
// message box shows on every game; a player's own action's shows on that player's game alone.
#include "Party.h"

using namespace PartyRules;

namespace
{
const LocalCall cNotification{
    .Class = "Debug", .Function = "Notification", .Arguments = {std::string("The ground shakes")}};
} // namespace

SCENARIO("A story's notification shows on every game", "[messages]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    party.WorldScript(host, cNotification);
    party.Until([&] { return !Party::Engine(guest).Messages.empty(); }, "the guest shows the host's notification");

    THEN("each game shows it once")
    {
        CHECK(Party::Engine(host).Messages == std::vector<std::string>{"The ground shakes"});
        CHECK(Party::Engine(guest).Messages == std::vector<std::string>{"The ground shakes"});
    }
}

SCENARIO("A message box a quest shows, from either game, shows on every game", "[messages]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();
    const LocalCall cBox{.Class = "Message", .Function = "Show", .Self = LocalForm{0x6A23B}};

    party.WorldScript(guest, cBox);
    party.Until([&] { return !Party::Engine(host).Messages.empty(); }, "the host shows the guest's quest box");
    CHECK(Party::Engine(guest).Messages == std::vector<std::string>{"6A23B"});
    CHECK(Party::Engine(host).Messages == std::vector<std::string>{"6A23B"});
}

SCENARIO("A player's own action's notification stays on that player's game", "[messages]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    party.As(guest, [&] { Party::Engine(guest).RunScript(cNotification, 0x14); });
    party.WorldScript(host, {.Class = "Debug", .Function = "Notification", .Arguments = {std::string("after")}});
    party.Until([&] { return Party::Engine(guest).Messages.size() == 2; },
                "the guest shows the host's later notification");

    THEN("the host never showed the guest's")
    CHECK(Party::Engine(host).Messages == std::vector<std::string>{"after"});
}

SCENARIO("A script equipping the player is no message of the party's", "[messages]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();

    party.WorldScript(
        host, {.Class = "Actor", .Function = "EquipItem", .Self = LocalForm{0x14}, .Arguments = {LocalForm{0x12EB7}}});
    party.WorldScript(host, cNotification);
    party.Until([&] { return !Party::Engine(guest).Messages.empty(); }, "the guest shows the notification after it");
    CHECK(Party::Engine(guest).Messages == std::vector<std::string>{"The ground shakes"});
}

SCENARIO("A story's message box: the host answers, the guest's copy closes with its answer", "[messages]")
{
    Party party;
    World& host = party.Host();
    World& guest = party.Guest();
    const LocalCall cBox{.Class = "Message", .Function = "Show", .Self = LocalForm{0x6A23B}};

    party.WorldScript(host, cBox);
    party.Until([&] { return !Party::Engine(guest).Boxes.empty(); }, "the guest shows the host's box");
    const uint32_t cHostBox = Party::Engine(host).Boxes.begin()->first;
    const uint32_t cGuestBox = Party::Engine(guest).Boxes.begin()->first;

    WHEN("the host's player presses its second button")
    {
        party.As(host, [&] { Party::Engine(host).AnswerBox(cHostBox, 1); });
        party.Until([&] { return Party::Engine(guest).Boxes.empty(); }, "the guest's box closes");
        THEN("the guest's copy got the host's answer")
        CHECK(Party::Engine(guest).Answers.at(cGuestBox) == 1);
    }
    WHEN("the guest presses its own copy first")
    {
        party.As(guest, [&] { Party::Engine(guest).AnswerBox(cGuestBox, 0); });
        THEN("the host's box still waits for the host") CHECK(Party::Engine(host).Boxes.contains(cHostBox));
        AND_WHEN("the host answers")
        {
            party.As(host, [&] { Party::Engine(host).AnswerBox(cHostBox, 1); });
            party.WorldScript(host, cNotification);
            party.Until(
                [&] {
                    return !Party::Engine(guest).Messages.empty() &&
                           Party::Engine(guest).Messages.back() == "The ground shakes";
                },
                "the guest sees the host's later notification");
            THEN("the guest's own answer stands") CHECK(Party::Engine(guest).Answers.at(cGuestBox) == 0);
        }
    }
}
