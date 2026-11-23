// The host walks ahead outdoors (playtest 2026-11-22, report 1951-468): its NPCs there reach a guest whose grid gets
// to them later, and a guest that stays out of the host's grid is brought back by the soft leash (party-travel.md).
#include "Party.h"
#include "Fixtures.h"

#include <Events/DisconnectedEvent.h>
#include <Services/Guest/GuestLeash.h>
#include <Services/Host/HostLeash.h>

namespace
{
constexpr uint32_t kTamriel = 0x0000003C;
constexpr uint32_t kFirstTamrielCell = 0x00020000; // the fake's: cells (-6..6, -6..6)
constexpr glm::vec3 kStart{100.f, 100.f, 0.f};      // cell (0, 0)
constexpr uint32_t kWolf = 0x000EBC4D;
constexpr uint32_t kWolfBase = 0x00023ABE;
constexpr glm::vec3 kWolfAt{4 * 4096.f + 300.f, 3 * 4096.f + 300.f, 0.f}; // cell (4, 3): out of a grid at (0, 0)

void OutdoorsWithAWolfAhead(FakeEngine& aEngine)
{
    aEngine.Exterior(kTamriel, kFirstTamrielCell, 6);
    aEngine.PlayerStands(aEngine.CellAt(kTamriel, kStart), kStart);
    aEngine.PlaceNpc(kWolf, kWolfBase, aEngine.CellAt(kTamriel, kWolfAt), kWolfAt);
    aEngine.Actors.at(kWolf).WorldSpace = kTamriel;
    aEngine.Actors.at(kWolf).NotLoadedYet = true;
}

glm::vec3 At(const FakeEngine& acEngine)
{
    const auto& cPlayer = acEngine.Actors.at(FakeEngine::kPlayer);
    return {cPlayer.Position[0], cPlayer.Position[1], cPlayer.Position[2]};
}

void Frames(Party& aParty, int aCount)
{
    for (int i = 0; i < aCount; ++i)
        aParty.Frame();
}
} // namespace

SCENARIO("The host walks ahead to a wolf the guest has not loaded: it shows once the guest's grid reaches it",
         "[copies][guest-own-npc][leash]")
{
    Party party(1, OutdoorsWithAWolfAhead);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) && PlayerCopy(guest); }, "both players are in");

    party.As(party.Host(), [&] { host.PlayerWalks({4 * 4096.f, 3 * 4096.f, 0.f}); });
    party.Until([&] { return host.ActorHas3D(kWolf); }, "the host's grid loads the wolf");
    Frames(party, 20);
    // Its cell never loaded here: no reference, so the host's spawn waits.
    REQUIRE_FALSE(guest.ActorExists(kWolf));
    REQUIRE_FALSE(guest.IsActorCopy(kWolf));

    party.As(party.Guest(), [&] { guest.PlayerWalks({3 * 4096.f, 2 * 4096.f, 0.f}); });
    party.Until([&] { return guest.IsActorCopy(kWolf) && guest.ActorHas3D(kWolf); },
                "the guest's grid reaches the wolf: the waiting spawn shows it as the host's copy",
                [&] { return std::format("exists {}, copy {}, 3D {}", guest.ActorExists(kWolf), guest.IsActorCopy(kWolf),
                                         guest.ActorHas3D(kWolf)); });
    party.Agree("the games agree on the wolf");
}

std::string CountdownLine(int aSeconds)
{
    return std::format("Too far from the host: teleporting in {}", aSeconds);
}

// What a game's player was told, in order: each change of the countdown's line on its screen (LeashCountdown), and the
// HUD's "Back with the host".
struct Told
{
    Party& Games;
    World& Game;
    std::vector<std::string> Lines{};
    std::string Last{};
    size_t Hud = 0;

    void Sample()
    {
        const auto* cpCountdown = Game.ctx().find<LeashCountdown>();
        const std::string cLine = cpCountdown && cpCountdown->Seconds ? CountdownLine(cpCountdown->Seconds) : "";
        if (cLine != Last && !cLine.empty())
            Lines.push_back(cLine);
        Last = cLine;
        const auto& cHud = Party::Engine(Game).HudLines;
        for (; Hud < cHud.size(); ++Hud)
            if (cHud[Hud] == "Back with the host")
                Lines.push_back(cHud[Hud]);
    }
    void Frames(int aCount)
    {
        for (int i = 0; i < aCount; ++i)
        {
            Games.Frame();
            Sample();
        }
    }
    // Frames until aDone, at most 70 s of them.
    void Until(const std::function<bool()>& aDone, const std::string& acStep)
    {
        for (int i = 0; i < 70 * 60 && !aDone(); ++i)
            Frames(1);
        if (!aDone())
            FAIL("never happened: " << acStep << " (told: " << Lines.size() << " lines, showing '" << Last << "')");
    }
    [[nodiscard]] bool Showing() const { return !Last.empty(); }
};

SCENARIO("A guest out of the host's grid sees a countdown, then is brought to the host", "[leash]")
{
    Party party(1, OutdoorsWithAWolfAhead);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) && PlayerCopy(guest); }, "both players are in");
    Told told{party, party.Guest()};
    Told hostTold{party, party.Host()};

    party.As(party.Guest(), [&] { guest.PlayerWalks({-4 * 4096.f, 100.f, 0.f}); });
    told.Until([&] { return told.Showing(); }, "the guest sees the countdown");
    CHECK(told.Lines == std::vector{CountdownLine(10)});
    // One line in place, never the HUD's queue (Skyrim shows a notification for seconds: ten would run behind).
    CHECK(guest.HudLines.empty());

    WHEN("it stays out")
    {
        told.Until([&] { return glm::distance(At(guest), At(host)) < 200.f; }, "the guest is brought to the host");
        THEN("the line counted each second once, 10 down to 1, and is gone")
        {
            std::vector<std::string> expected;
            for (int second = 10; second >= 1; --second)
                expected.push_back(CountdownLine(second));
            told.Frames(5 * 60);
            CHECK(told.Lines == expected);
            CHECK_FALSE(told.Showing());
            hostTold.Sample();
            CHECK(hostTold.Lines.empty());
        }
        party.Agree("the games agree on the players");

        AND_WHEN("it walks out again")
        {
            const size_t cTold = told.Lines.size();
            party.As(party.Guest(), [&] { guest.PlayerWalks({100.f, -4 * 4096.f, 0.f}); });
            told.Until([&] { return told.Lines.size() > cTold; }, "a new countdown starts");
            CHECK(told.Lines.back() == CountdownLine(10));
        }
    }

    WHEN("it walks back in time")
    {
        told.Frames(5 * 60);
        party.As(party.Guest(), [&] { guest.PlayerWalks({4096.f, 100.f, 0.f}); });
        told.Until([&] { return told.Lines.back() == "Back with the host"; }, "the countdown stops");
        CHECK_FALSE(told.Showing());
        const auto cTold = told.Lines;
        told.Frames(10 * 60);
        CHECK(glm::distance(At(guest), glm::vec3{4096.f, 100.f, 0.f}) < 1.f);
        CHECK(told.Lines == cTold);
    }

    WHEN("the host walks to it in time")
    {
        told.Frames(3 * 60);
        party.As(party.Host(), [&] { host.PlayerWalks({-3 * 4096.f, 100.f, 0.f}); });
        told.Until([&] { return told.Lines.back() == "Back with the host"; }, "the countdown stops");
        told.Frames(10 * 60);
        CHECK(glm::distance(At(guest), glm::vec3{-4 * 4096.f, 100.f, 0.f}) < 1.f);
    }

    WHEN("the host goes indoors mid-countdown")
    {
        told.Frames(3 * 60);
        constexpr uint32_t cInn = 0x000133C9;
        party.As(party.Host(), [&] { host.PlayerTravels(cInn, {10.f, 10.f, 0.f}); });
        told.Until([&] { return guest.GetPlayerParentCellId() == cInn; }, "the party tether brings the guest indoors");
        const auto cTold = told.Lines;
        told.Frames(12 * 60);
        THEN("the countdown stopped without a word, and nothing moved it again")
        {
            CHECK_FALSE(told.Showing());
            CHECK(told.Lines == cTold);
            CHECK(std::ranges::find(cTold, "Back with the host") == cTold.end());
            CHECK(guest.GetPlayerParentCellId() == cInn);
        }
    }

    WHEN("the guest leaves the session mid-countdown")
    {
        told.Frames(3 * 60);
        party.As(party.Guest(), [&] { party.Guest().GetDispatcher().trigger(DisconnectedEvent{}); });
        told.Sample();
        THEN("no line is left on its screen") CHECK_FALSE(told.Showing());
    }
}

// PR 109 review: its game holds the trip as its tether's target, so the host's repeats must still move it.
SCENARIO("A leashed guest whose first move never happened is moved by the host's repeat", "[leash]")
{
    Party party(1, OutdoorsWithAWolfAhead);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) && PlayerCopy(guest); }, "both players are in");
    Told told{party, party.Guest()};

    party.As(party.Guest(), [&] { guest.PlayerWalks({-4 * 4096.f, 100.f, 0.f}); });
    guest.DropPlayerMoves = 1;
    told.Until([&] { return guest.DropPlayerMoves == 0; }, "the countdown ends and the guest's move is lost");
    CHECK(glm::distance(At(guest), At(host)) > 3 * 4096.f);
    told.Until([&] { return glm::distance(At(guest), At(host)) < 200.f; }, "a repeat of the trip brings the guest to the host");
    party.Agree("the games agree on the players");
}

SCENARIO("With two guests, only the one out of the host's grid is counted down and brought back", "[leash]")
{
    Party party(2, OutdoorsWithAWolfAhead);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& stays = Party::Engine(party.Guest(0));
    FakeEngine& strays = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopies(host).size() == 2; }, "both guests are in");
    party.As(party.Guest(0), [&] { stays.PlayerWalks({2 * 4096.f, 100.f, 0.f}); });
    const glm::vec3 cStayed = At(stays);
    Told stayTold{party, party.Guest(0)};
    Told strayTold{party, party.Guest(1)};

    party.As(party.Guest(1), [&] { strays.PlayerWalks({100.f, 4 * 4096.f, 0.f}); });
    for (int i = 0; i < 40 * 60 && glm::distance(At(strays), At(host)) >= 200.f; ++i)
    {
        party.Frame();
        stayTold.Sample();
        strayTold.Sample();
    }
    REQUIRE(glm::distance(At(strays), At(host)) < 200.f);
    CHECK(stayTold.Lines.empty());
    CHECK(At(stays) == cStayed);
    CHECK(strayTold.Lines.size() == 10);
}

// The host walks off, so the cell its copy of the guest stands in detaches there; then the guest catches up: its copy
// follows by position but keeps the detached cell, gets no UpdateAI and drops below high process, unseen
// (Skyrim 2026-11-22, player-copies-after-setpos). It is moved into the cell it stands in.
SCENARIO("A player's copy left in a cell that detached is moved into the cell it stands in", "[copies][leash]")
{
    Party party(1, OutdoorsWithAWolfAhead);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) && PlayerCopy(guest); }, "both players are in");
    const uint32_t cCopy = PlayerCopy(host);
    const uint32_t cStart = host.Actors.at(cCopy).Cell;

    party.As(party.Host(), [&] { host.PlayerWalks({4 * 4096.f, 100.f, 0.f}); });
    party.Until([&] { return !host.Attached.contains(cStart); }, "the copy's cell detaches on the host");
    party.As(party.Guest(), [&] { guest.PlayerWalks({4 * 4096.f - 300.f, 100.f, 0.f}); });

    const auto cWhere = [&] {
        const auto& cActor = host.Actors.at(cCopy);
        return std::format("cell {:X} (stands in {:X}), attached {}", cActor.Cell,
                           host.CellAt(kTamriel, {cActor.Position[0], cActor.Position[1], 0.f}), host.Attached.contains(cActor.Cell));
    };
    party.Until([&] {
        const auto& cActor = host.Actors.at(cCopy);
        return cActor.Position[0] > 3 * 4096.f && host.Attached.contains(cActor.Cell) &&
               cActor.Cell == host.CellAt(kTamriel, {cActor.Position[0], cActor.Position[1], 0.f});
    }, "the host's copy of the guest is in the cell it stands in", cWhere, nullptr, std::chrono::seconds(10));
    party.Agree("the games agree on the players");
}
