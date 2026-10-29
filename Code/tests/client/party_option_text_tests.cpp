// The party's dialogue list as each menu shows it (docs/design/shared-dialogue.md): who can say an option.
#include <catch2/catch.hpp>

#include <Services/PartyOptionText.h>

TEST_CASE("An option everyone has shows its text; one only some have, their names", "[dialogue]")
{
    CHECK(PartyOptionText("Here. Have a gold piece.", {}) == "Here. Have a gold piece.");
    CHECK(PartyOptionText("I have your claw.", {"Lydia"}) == "I have your claw. [Lydia]");
    CHECK(PartyOptionText("(Persuade) Surely you can help.", {"Lydia", "Aela"}) == "(Persuade) Surely you can help. [Lydia, Aela]");
}

TEST_CASE("Another player's option: this player cannot say it, and its chat says so by name", "[dialogue]")
{
    CHECK(CanSay({}, 7));         // everyone's
    CHECK(CanSay({3, 7}, 7));
    CHECK_FALSE(CanSay({3}, 7));
    CHECK(CannotSayText("Aela") == "I'm sorry Aela, you don't know how to say that.");
}
