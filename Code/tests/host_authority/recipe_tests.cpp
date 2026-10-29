// Player-made potions' recipes (docs/design/crafted-potions.md): a guest's become the host's, and each game gets the
// ones it has not had before the host's next message to it.
#include "session.h"

namespace
{
Recipe Fortify()
{
    return {false, {{25.f, 0, 60, 30.f, GameId(0, 0x3EAF3)}}};
}

// Where in what aTo was sent the first message of type T is (the outbox's order), -1 for none.
template <class T> int FirstTo(const Session& acSession, ConnectionId_t aTo)
{
    int index = 0;
    for (const auto& cSent : acSession.Outbox)
    {
        if (cSent.To != aTo)
            continue;
        if (dynamic_cast<const T*>(cSent.Message.get()))
            return index;
        ++index;
    }
    return -1;
}
} // namespace

TEST_CASE("A guest's recipes are the host's; another game gets them before the next message naming one", "[HostAuthority][Recipes]")
{
    Session session;
    RecipesRequest brewed;
    brewed.Known = {Fortify()};
    session.Receive(kPeer, brewed);
    CHECK(session.Recipes.Find(Fortify().Id()) == Fortify());
    CHECK(session.Outbox.empty());

    // A message relayed to the others: the recipes first.
    SystemQuestsRequest learned;
    learned.Quests = {GameId(0, 0xEAFD5)};
    session.Receive(kPeer, learned);
    REQUIRE(FirstTo<NotifyRecipes>(session, kOtherPeer) == 0);
    CHECK(FirstTo<NotifySystemQuests>(session, kOtherPeer) == 1);
    CHECK(session.SentTo<NotifyRecipes>(kOtherPeer)[0]->Known == brewed.Known);
    THEN("the host's own game shares the book") CHECK(session.SentTo<NotifyRecipes>(kHost).empty());

    // Told once.
    session.Outbox.clear();
    session.Receive(kPeer, learned);
    CHECK(session.SentTo<NotifyRecipes>(kOtherPeer).empty());
    CHECK(session.SentTo<NotifySystemQuests>(kOtherPeer).size() == 1);
}

TEST_CASE("A game joining later gets every recipe before anything else", "[HostAuthority][Recipes]")
{
    Session session;
    session.Recipes.Learn(Fortify()); // the host's player's own potion
    constexpr ConnectionId_t cLate = 4;
    session.Receive(cLate, JoinRequest("Serana", 30));
    REQUIRE(FirstTo<NotifyRecipes>(session, cLate) == 0);
    CHECK(session.SentTo<NotifyRecipes>(cLate)[0]->Known == std::vector{Fortify()});
    CHECK(!session.SentTo<AuthenticationResponse>(cLate).empty());
}

TEST_CASE("A game that left and comes back is told the book again", "[HostAuthority][Recipes]")
{
    Session session;
    session.Recipes.Learn(Fortify());
    SystemQuestsRequest learned;
    learned.Quests = {GameId(0, 0xEAFD5)};
    session.Receive(kOtherPeer, learned);
    REQUIRE(session.SentTo<NotifyRecipes>(kPeer).size() == 1);
    session.Leave(kPeer);
    session.Outbox.clear();
    session.Receive(kPeer, JoinRequest("Lydia", 12));
    CHECK(FirstTo<NotifyRecipes>(session, kPeer) == 0);
}
