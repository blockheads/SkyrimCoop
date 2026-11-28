// Questions about the player with no player's action behind them ask about the party (docs/design/party-model.md,
// rule 3; party-calls.md, "Questions"): a quest's timer measuring how far the player is from its marker answers for
// the nearest player.
#include "Party.h"
#include "Fixtures.h"

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kGuard = 0x0001A67C;


} // namespace

SCENARIO("A quest's timer asks how far the player is: the nearest player answers", "[questions]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("the players are present");
    party.As(party.Host(), [&] { host.PlayerStands(kSmoke, {2000.f, 0.f, 0.f}); });
    party.As(party.Guest(), [&] { guest.PlayerStands(kSmoke, {110.f, 0.f, 0.f}); });
    party.Agree("the guest's movement reaches its copy");
    party.Until([&] {
        const auto cCopies = host.CopiesOf();
        return !cCopies.empty() && std::abs(host.Actors.at(cCopies[0]).Position[0] - 110.f) < 1.f;
    }, "the player's copy reaches the distance fixture");
    uint32_t marker{};
    party.As(party.Host(), [&] { marker = host.LoadNpc(kGuard, kSmoke, {100.f, 0.f, 0.f}); });
    const LocalCall cAsks{.Class = "ObjectReference",
                          .Function = "GetDistance",
                          .Self = LocalForm{marker},
                          .Arguments = {LocalForm{FakeEngine::kPlayer}}};

    WHEN("no player's action is behind it")
    {
        party.As(party.Host(), [&] { host.RunScript(cAsks); });
        THEN("it is the guest's distance, the nearer") CHECK(std::get<float>(*host.LastAnswer) < 50.f);
    }
    WHEN("the host's player's own action asks it")
    {
        party.As(party.Host(), [&] { host.RunScript(cAsks, 0x14); });
        THEN("it is the host's own distance") CHECK(std::get<float>(*host.LastAnswer) > 1000.f);
    }
}

SCENARIO("A quest's timer asks whether a guard sees the player: yes if it sees any player", "[questions]")
{
    Party party(1, SmokeWorld);
    auto& host = Party::Engine(party.Host());
    auto& guest = Party::Engine(party.Guest());
    party.Agree("the players are present");
    party.As(party.Host(), [&] { host.PlayerStands(kSmoke, {5000.f, 0.f, 0.f}); });
    party.As(party.Guest(), [&] { guest.PlayerStands(kSmoke, {300.f, 0.f, 0.f}); });
    party.Agree("the guest's movement reaches its copy");
    uint32_t guard{};
    party.As(party.Host(), [&] { guard = host.LoadNpc(kGuard, kSmoke, {0.f, 0.f, 0.f}); });
    const LocalCall cSees{.Class = "Actor",
                          .Function = "HasLOS",
                          .Self = LocalForm{guard},
                          .Arguments = {LocalForm{FakeEngine::kPlayer}}};

    WHEN("no player's action is behind it")
    {
        party.As(party.Host(), [&] { host.RunScript(cSees); });
        THEN("the guest in sight makes it yes, though the host is out of it") CHECK(std::get<bool>(*host.LastAnswer));
    }
    WHEN("the host's player's own action asks it")
    {
        party.As(party.Host(), [&] { host.RunScript(cSees, 0x14); });
        THEN("only the host's own sight counts") CHECK(!std::get<bool>(*host.LastAnswer));
    }
}

// A word wall (WordWallTriggerScript, Bleak Falls Barrow): its OnUpdate asks Game.GetPlayer().HasLOS(lookTarget), an
// xmarker, then how far the player is from it. The engine answers a player's sight from its own camera and refuses any
// other actor's sight of an object (54698), so on the host a guest's sight is its game's answer (GuestSight), and the
// wall's timer asks the party (docs/re/systems/papyrus-guest-scripts.md, "A player's line of sight").
namespace
{
constexpr uint32_t kWallMarker = 0x000709D4; // lookTargetWord01, an xmarker

struct WallParty
{
    Party party{1, SmokeWorld};
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    uint32_t copy = 0;

    // Each player stands that far from the marker; the guest's copy reaches its place on the host.
    WallParty(float aHostGap, float aGuestGap)
    {
        party.Agree("the players are present");
        for (FakeEngine* pEngine : {&host, &guest})
            pEngine->References[kWallMarker].Position = {0.f, 0.f, 0.f};
        party.As(party.Host(), [&] { host.PlayerStands(kSmoke, {aHostGap, 0.f, 0.f}); });
        party.As(party.Guest(), [&] { guest.PlayerStands(kSmoke, {0.f, aGuestGap, 0.f}); });
        party.Until([&] {
            const auto cCopies = host.CopiesOf();
            return !cCopies.empty() && std::abs(host.Actors.at(cCopies[0]).Position[1] - aGuestGap) < 1.f;
        }, "the guest's copy stands where the guest does");
        copy = host.CopiesOf()[0];
    }

    // The wall's call on the host in the guest's stack (GetPlayer is the guest's character), answered now.
    LocalValue Ask(const char* acpClass, const char* acpFunction, bool aTimer)
    {
        party.As(party.Host(), [&] {
            host.RunScript({.Class = acpClass, .Function = acpFunction, .Self = LocalForm{copy}, .Arguments = {LocalForm{kWallMarker}}},
                           copy, false, false, aTimer);
        });
        return *host.LastAnswer;
    }
    bool Sees(bool aTimer)
    {
        return std::get<bool>(Ask("Actor", "HasLOS", aTimer));
    }
};
} // namespace

SCENARIO("A guest looks at a word wall's marker: its own game answers the host's script", "[questions][sight]")
{
    WallParty wall(5000.f, 300.f);
    THEN("the host never sees the guest's look itself: the first answer is no")
        CHECK_FALSE(wall.Sees(false));
    THEN("the guest's game answers, and the host's script hears yes")
        wall.party.Until([&] { return wall.Sees(false); }, "the wall's script reads the guest's sight");
}

SCENARIO("A word wall's timer asks the party: whoever looks counts, in whoever's loop", "[questions][sight]")
{
    WallParty wall(300.f, 5000.f);
    WHEN("the guest walked in first: the loop is the guest's, and only the host looks")
    {
        THEN("the guest's own question stays the guest's") CHECK_FALSE(wall.Sees(false));
        THEN("the timer's question is the party's: the host's look counts") CHECK(wall.Sees(true));
        THEN("the timer's distance is the nearest player's")
        {
            CHECK(std::get<float>(wall.Ask("ObjectReference", "GetDistance", true)) < 400.f);
            CHECK(std::get<float>(wall.Ask("ObjectReference", "GetDistance", false)) > 4000.f);
        }
    }
}

SCENARIO("A word wall's timer shows its screen to every game", "[questions][screen]")
{
    WallParty wall(300.f, 300.f);
    const auto cShake = [&](bool aTimer) {
        wall.party.As(wall.party.Host(), [&] {
            wall.host.RunScript({.Class = "Game", .Function = "ShakeCamera", .Arguments = {std::monostate{}, 0.5f, 2.0f}},
                                wall.copy, false, false, aTimer);
        });
    };
    WHEN("the guest's own action shakes the camera")
    {
        cShake(false);
        wall.party.Until([&] { return wall.guest.ShakeSeconds > 0; }, "the guest's screen shakes");
        THEN("only the guest's") CHECK(wall.host.ShakeSeconds <= 0);
    }
    WHEN("the wall's timer in the guest's loop shakes it")
    {
        cShake(true);
        wall.party.Until([&] { return wall.guest.ShakeSeconds > 0; }, "the guest's screen shakes");
        THEN("the host's shakes too") CHECK(wall.host.ShakeSeconds > 0);
    }
}
