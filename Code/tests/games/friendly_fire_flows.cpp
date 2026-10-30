// Friendly fire (docs/design/friendly-fire.md): a player's hit or spell on another player's copy goes to that
// player's game, which applies it to its real player; a teammate's kill is a real death, named in every chat.
#include "Party.h"
#include "Fixtures.h"

#include <Events/SessionNoticeEvent.h>
#include <Events/MeleeStaggerEvent.h>
#include <Services/DeathService.h>
#include <SessionIds.h>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kFirebolt = 0x0001C789;
constexpr uint32_t kFireboltEffect = 0x00012EF7;

// aIn's copy of aOf's player (0: none yet).
uint32_t CopyOfPlayer(Party& aParty, World& aIn, World& aOf)
{
    uint32_t session = 0, copy = 0;
    aParty.As(aOf, [&] { session = Utils::GetSessionIdByFormId(FakeEngine::kPlayer).value_or(0); });
    if (session)
        aParty.As(aIn, [&] { copy = Utils::GetFormIdBySessionId(session); });
    return copy;
}

// aGame's chat lines about the session.
struct ChatNotices
{
    explicit ChatNotices(World& aGame)
        : Connection(aGame.GetDispatcher().sink<SessionNoticeEvent>().connect<&ChatNotices::On>(*this))
    {
    }
    void On(const SessionNoticeEvent& acNotice) { Lines.push_back(acNotice.Text); }
    [[nodiscard]] bool Has(const std::string& acLine) const { return std::ranges::find(Lines, acLine) != Lines.end(); }
    std::vector<std::string> Lines;
    entt::scoped_connection Connection;
};

float Health(FakeEngine& aEngine)
{
    return aEngine.GetActorValue(FakeEngine::kPlayer, FakeEngine::kHealth);
}

// aAttacker's player strikes aVictim's player's copy on aAttacker's game until aVictim's own player has taken it.
void Strike(Party& aParty, World& aAttacker, World& aVictim, float aDamage)
{
    FakeEngine& attacker = Party::Engine(aAttacker);
    FakeEngine& victim = Party::Engine(aVictim);
    aParty.Until([&] { return CopyOfPlayer(aParty, aAttacker, aVictim) != 0; },
                 std::format("{}'s game has {}'s player", attacker.Name, victim.Name));
    const float cBefore = Health(victim);
    const uint32_t cCopy = CopyOfPlayer(aParty, aAttacker, aVictim);
    aParty.As(aAttacker, [&] { attacker.Strikes(FakeEngine::kPlayer, cCopy, aDamage); });
    aParty.Until([&] { return Health(victim) == cBefore - aDamage; },
                 std::format("{}'s own player takes {}'s blow", victim.Name, attacker.Name));
}
} // namespace

SCENARIO("A guest's sword on the host's player: the host's game applies it, and nobody turns on the guest",
         "[combat][friendly-fire]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    const float cFull = Health(host);

    Strike(party, party.Guest(), party.Host(), 30.f);
    THEN("the guest's copy of the host took nothing itself, and the host's player ran no NPC's reaction")
    {
        CHECK(guest.GetActorValue(CopyOfPlayer(party, party.Guest(), party.Host()), FakeEngine::kHealth) <= cFull);
        CHECK(std::ranges::none_of(host.Reactions, [](const auto& acReaction) { return acReaction.first == FakeEngine::kPlayer; }));
    }
    party.Until([&] { return guest.GetActorValue(CopyOfPlayer(party, party.Guest(), party.Host()), FakeEngine::kHealth) == cFull - 30.f; },
                "the guest's copy of the host shows the host's health");
}

SCENARIO("The host's sword on a guest's player: the guest's game applies it", "[combat][friendly-fire]")
{
    Party party(1, SmokeWorld);
    Strike(party, party.Host(), party.Guest(), 25.f);
}

SCENARIO("A guest's sword on another guest's player: the host passes it to that guest's game", "[combat][friendly-fire]")
{
    Party party(2, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    const float cHostFull = Health(host);
    Strike(party, party.Guest(), party.Guest(1), 20.f);
    CHECK(Health(host) == cHostFull);
}

SCENARIO("A guest's fire bolt on the host's player burns the host's own player", "[magic][friendly-fire]")
{
    Party party(1, SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return CopyOfPlayer(party, party.Guest(), party.Host()) != 0; }, "the guest's game has the host's player");
    const float cFull = Health(host);

    const uint32_t cCopy = CopyOfPlayer(party, party.Guest(), party.Host());
    party.As(party.Guest(), [&] { guest.SpellHits(cCopy, FakeEngine::kPlayer, kFirebolt, kFireboltEffect, 40.f); });
    party.Until([&] { return Health(host) == cFull - 40.f; }, "the host's player takes the bolt");
    THEN("the effect landed on the host's player, from the guest's copy, with no NPC's reaction")
    {
        CHECK(std::ranges::any_of(host.Effects, [&](const auto& acEffect) {
            return acEffect.Target == FakeEngine::kPlayer && acEffect.Spell == kFirebolt &&
                   acEffect.Caster == CopyOfPlayer(party, party.Host(), party.Guest());
        }));
        CHECK(host.Reactions.empty());
    }
}

SCENARIO("A guest killed by another guest: a real death, spectating, and every chat names the killer",
         "[death][friendly-fire]")
{
    Party party(2, SmokeWorld);
    FakeEngine& victim = Party::Engine(party.Guest(1));
    const ChatNotices cHostChat(party.Host());
    const ChatNotices cKillerChat(party.Guest());
    party.Agree("all players are present");

    Strike(party, party.Guest(), party.Guest(1), Health(victim));
    auto& death = party.Guest(1).ctx().at<DeathService>();
    party.Until([&] { return death.IsDown(); }, "the victim goes down");
    CHECK(!victim.IsPlayerAlive());
    CHECK(victim.Actors.at(FakeEngine::kPlayer).KilledBy == CopyOfPlayer(party, party.Guest(1), party.Guest()));
    party.Until([&] { return cHostChat.Has("Aela was killed by Lydia.") && cKillerChat.Has("Aela was killed by Lydia."); },
                "every chat says who killed the victim");
    party.Until([&] { return death.Spectating() != 0; }, "the victim spectates once dead", [&] {
        std::string out = std::format("down {} dead {} dying {}; ", death.IsDown(), victim.Actors.at(FakeEngine::kPlayer).Dead,
                                      victim.Actors.at(FakeEngine::kPlayer).DyingFrames);
        for (const uint32_t cId : victim.CopiesOf())
            if (victim.Actors.at(cId).Player)
                out += std::format("copy {:X} dead {} 3d {}; ", cId, victim.Actors.at(cId).Dead, victim.ActorHas3D(cId));
        return out;
    });
}

SCENARIO("A player's melee stagger reaches the victim's owner once", "[friendly-fire][melee-stagger]")
{
    Party party(2, SmokeWorld);
    party.Agree("all players and copies are present");
    auto& attacker = Party::Engine(party.Guest());
    const bool cOtherGuest = GENERATE(false, true);
    World& victimGame = cOtherGuest ? party.Guest(1) : party.Host();
    auto& victim = Party::Engine(victimGame);
    uint32_t targetSession = 0, targetCopy = 0;
    party.As(victimGame, [&] { targetSession = *Utils::GetSessionIdByFormId(FakeEngine::kPlayer); });
    party.As(party.Guest(), [&] { targetCopy = Utils::GetFormIdBySessionId(targetSession); });
    REQUIRE(targetCopy);
    party.As(party.Guest(), [&] {
        party.Guest().GetDispatcher().trigger(MeleeStaggerEvent{targetCopy, FakeEngine::kPlayer, 0.75f});
    });
    party.Until([&] { return victim.Staggers.size() == 1; }, "the victim's own game staggers it");
    CHECK(std::get<0>(victim.Staggers[0]) == FakeEngine::kPlayer);
    CHECK(std::get<2>(victim.Staggers[0]) == Approx(0.75f));
    CHECK(attacker.Staggers.empty());
    const auto cHealth = victim.GetActorValue(FakeEngine::kPlayer, 24);
    for (int i = 0; i < 10; ++i)
        party.Frame();
    CHECK(victim.Staggers.size() == 1);
    CHECK(victim.GetActorValue(FakeEngine::kPlayer, 24) == cHealth);
}
