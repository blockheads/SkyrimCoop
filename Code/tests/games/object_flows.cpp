// Objects across games (ObjectService): a guest's activation of an object runs on the host's game, by its copy; the
// other games get what changed as the host's rows. A locked one's is a lockpick attempt, not passed on.
#include "Party.h"
#include "Fixtures.h"

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <cstring>

namespace
{
constexpr uint32_t kSmoke = 0x00031D2E;
constexpr uint32_t kLever = 0x000F1B2C; // a placed reference in QASmoke
constexpr uint32_t kGate = 0x000F1B2D;  // a locked one
constexpr uint32_t kChest = 0x000C2CE8; // QASmoke's small chest, a lid that opens
constexpr uint32_t kPlant = 0x000F1B2E; // a flora reference (a mountain flower)
constexpr uint32_t kPullLever = 0x000F1B2F; // a lever furniture (Bleak Falls Barrow's pillar lever's kind)
constexpr uint32_t kCritter = 0x000F1B30;   // a reference with no body: a fish a script's TranslateTo moves

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
    aEngine.References[kLever] = {.Cell = kSmoke};
    aEngine.References[kGate] = {.Cell = kSmoke, .Locked = true, .LockLevel = 50};
    aEngine.References[kChest] = {.Cell = kSmoke, .Chest = true};
    aEngine.References[kPlant] = {.Cell = kSmoke, .Plant = true};
    aEngine.References[kPullLever] = {.Cell = kSmoke, .Lever = true};
    aEngine.References[kCritter] = {.Cell = kSmoke};
}

// The info lines every game logs while it lives; the other sinks keep the level they had.
struct InfoLines
{
    InfoLines()
        : m_level(spdlog::get_level())
    {
        for (auto& pSink : spdlog::default_logger()->sinks())
            pSink->set_level(m_level);
        spdlog::default_logger()->sinks().push_back(m_pLines);
        spdlog::set_level(spdlog::level::info);
    }
    ~InfoLines()
    {
        spdlog::set_level(m_level);
        auto& sinks = spdlog::default_logger()->sinks();
        std::erase(sinks, m_pLines);
        for (auto& pSink : sinks)
            pSink->set_level(spdlog::level::trace);
    }
    [[nodiscard]] size_t Count(std::string_view aText) const
    {
        return std::ranges::count_if(m_pLines->last_formatted(), [aText](const std::string& acLine) { return acLine.contains(aText); });
    }

private:
    spdlog::level::level_enum m_level;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_pLines = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(4096);
};


} // namespace

SCENARIO("A guest pulling a lever pulls it on the host's game, by its copy", "[objects]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] {
        guest.Activates(FakeEngine::kPlayer, kGate);
        guest.Activates(FakeEngine::kPlayer, kLever);
    });
    party.Until([&] { return std::ranges::count(host.Activations, std::pair{kLever, PlayerCopy(host)}) == 1; },
                "the host's copy of the guest pulls the lever");
    THEN("the locked gate's activation was a lockpick attempt, not passed on")
    CHECK(std::ranges::count(host.Activations, std::pair{kGate, PlayerCopy(host)}) == 0);
}

SCENARIO("A lever a guest pulls moves once, on the host, and no other guest replays it", "[objects]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) != 0 && PlayerCopy(other) != 0; }, "every game has every player");

    party.As(party.Guest(0), [&] { guest.Activates(FakeEngine::kPlayer, kLever); });
    party.Until([&] { return host.Activations.size() == 1; }, "the host's game runs the guest's pull");
    // A marker from the host after it: the other guest has had whatever came before.
    party.As(party.Host(), [&] { host.PlayerSetsLock(kGate, false, 50); });
    party.Until([&] { return !other.References[kGate].Locked; }, "the other guest has the host's later change");

    THEN("the other guest replayed nothing")
    CHECK(other.Activations.empty());
}

// Playtest 2026-11-22 item 11; scenario guest-pulls-lever-furniture; docs/re/systems/activation.md, "Lever furniture".
SCENARIO("A lever furniture one player pulls moves once on every game", "[objects]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) != 0 && PlayerCopy(other) != 0; }, "every game has every player");
    const auto pulls = [](FakeEngine& aEngine) { return aEngine.References.at(kPullLever).GraphEvents.size(); };
    // A marker from the host after the pull: every guest has had whatever came before.
    const auto settle = [&] {
        party.As(party.Host(), [&] { host.PlayerSetsLock(kGate, false, 50); });
        party.Until([&] { return !guest.References[kGate].Locked && !other.References[kGate].Locked; }, "both guests have the host's later change");
    };

    WHEN("a guest pulls it")
    {
        party.As(party.Guest(0), [&] { guest.Activates(FakeEngine::kPlayer, kPullLever); });
        party.Until([&] { return pulls(host) == 1 && pulls(other) == 1; }, "the host's lever and the other guest's move");
        settle();
        THEN("the guest's own moved once, by its own engine")
        CHECK(pulls(guest) == 1);
    }
    WHEN("the host pulls it")
    {
        party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, kPullLever); });
        party.Until([&] { return pulls(guest) == 1 && pulls(other) == 1; }, "both guests' levers move");
        settle();
        CHECK(pulls(host) == 1);
    }
    WHEN("its script blocks activation and each player pulls it")
    {
        for (FakeEngine* pEngine : {&host, &guest, &other})
            pEngine->References.at(kPullLever).ActivationBlocked = true;
        party.As(party.Guest(0), [&] { guest.Activates(FakeEngine::kPlayer, kPullLever); });
        party.Until([&] { return host.Activations.size() == 1; }, "the host's game runs the guest's pull");
        party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, kPullLever); });
        settle();
        THEN("no game's lever moved: its script decides")
        CHECK(pulls(host) + pulls(guest) + pulls(other) == 0);
    }
}

// A scripted activator (NorLever01SCRIPT, defaultButtonSCRIPT, NorPullBar01SCRIPT, a claw door's ring): its host
// script's PlayAnimation is a row; a guest's own script never ran (WorldEvents drops its OnActivate).
SCENARIO("A guest's pull of a scripted lever moves it on every game by the host's script", "[objects]")
{
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) != 0 && PlayerCopy(other) != 0; }, "every game has every player");

    party.As(party.Guest(0), [&] { guest.Activates(FakeEngine::kPlayer, kLever); });
    party.Until([&] { return host.Activations.size() == 1; }, "the host's game runs the guest's pull");
    party.As(party.Host(), [&] { host.ScriptPlaysAnimation(kLever, "FullPull"); }); // its OnActivate, on the host
    party.Until([&] { return guest.References[kLever].GraphEvents.size() == 1 && other.References[kLever].GraphEvents.size() == 1; },
                "both guests' levers play the host's animation");
    CHECK(guest.References[kLever].GraphEvents.front() == "FullPull");
}

SCENARIO("A reference the host's script moves every frame follows on the guest without a log line a frame", "[objects]")
{
    // Josh's playtest (2026-11-26): a guest logged "applied N of the host's object changes" for critters' moves 9,400
    // times in 4 minutes. TranslateTo moves its reference each frame (18898), so its move row goes out each frame.
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    InfoLines lines;
    constexpr int cFrames = 60;
    for (int i = 1; i <= cFrames; ++i)
    {
        const float cPose[6]{100.f + static_cast<float>(i), 200.f, -60.f, 0.f, 0.f, 0.5f};
        ObjectChange move{.Type = ObjectChange::Kind::kMove, .Object = GameId(0, kCritter)};
        move.Data.assign(reinterpret_cast<const char*>(cPose), sizeof(cPose));
        party.As(party.Host(), [&] { host.Recorded.push_back(move); });
        party.Frame();
    }
    party.Until([&] { return guest.References[kCritter].Position.x == 100.f + cFrames; }, "the guest's critter follows to its last spot");
    THEN("neither game logged a line for its moves")
    CHECK(lines.Count("object changes") == 0);
}

SCENARIO("A plant one player picks is picked on every game", "[objects]")
{
    // The review of PR #87: no game replays another's activation, so a pick reaches the others as the host's row.
    Party party(2, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest(0));
    FakeEngine& other = Party::Engine(party.Guest(1));
    party.Until([&] { return PlayerCopy(host, 1) != 0; }, "the host's game has both guests");

    WHEN("the host picks it")
    {
        party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, kPlant); });
        party.Until([&] { return guest.References[kPlant].Harvested && other.References[kPlant].Harvested; },
                    "both guests show it picked");
    }
    WHEN("a guest picks it")
    {
        party.As(party.Guest(0), [&] { guest.Activates(FakeEngine::kPlayer, kPlant); });
        party.Until([&] { return host.References[kPlant].Harvested && other.References[kPlant].Harvested; },
                    "the host and the other guest show it picked");
    }
}

// Scenario guest-chest-lid-after-disable; docs/re/systems/guest-world-events.md, "A lid after a disable".
SCENARIO("A chest the host disables under a guest's open menu shows one lid once enabled", "[objects]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");

    party.As(party.Guest(), [&] { guest.Activates(FakeEngine::kPlayer, kChest); });
    party.Until([&] { return std::ranges::count(host.Activations, std::pair{kChest, PlayerCopy(host)}) == 1; },
                "the host's copy of the guest opens the chest");
    REQUIRE(guest.References.at(kChest).Lid == 1.f);
    REQUIRE(host.References.at(kChest).Lid == 0.f);

    party.As(party.Host(), [&] { host.ScriptSetsEnabled(kChest, false); });
    party.Until([&] { return guest.References.at(kChest).Disabled; }, "the guest's chest is disabled");
    party.As(party.Guest(), [&] { guest.ClosesChestMenu(); }); // the live menu's auto-close
    party.As(party.Host(), [&] { host.ScriptSetsEnabled(kChest, true); });
    party.Until([&] { return !guest.References.at(kChest).Disabled; }, "the guest's chest is enabled");
    party.As(party.Guest(), [&] { guest.Finishes3D(kChest); });
    party.Until([&] { return guest.References.at(kChest).Lid == 0.f && host.References.at(kChest).Lid == 0.f; },
                "both lids closed, as both chests' open states read");
    CHECK_FALSE(guest.References.at(kChest).Open);
}

// warehouse_traps_hurt_both (2026-11-23): a tripped tripwire's script marks it destroyed (form flag 0x800000); the
// guest's stayed whole, the world check finding it on every tripwire.
SCENARIO("A reference the host's script marks destroyed is destroyed on the guest", "[objects]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(host) != 0; }, "the host's game has the guest's player");
    party.As(party.Host(), [&] { host.ScriptSetsDestroyed(kChest, true); });
    party.Until([&] { return guest.References.at(kChest).Destroyed; }, "the guest's is destroyed");
    CHECK_FALSE(guest.References.at(kChest).Disabled);
}

// Scenario host-menus-close-when-target-goes: the same on the host's own chest.
SCENARIO("A chest the host disables under its own open menu shows one lid once enabled", "[objects]")
{
    Party party(1, Plugins);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return PlayerCopy(guest) != 0; }, "the guest's game has the host's player");

    party.As(party.Host(), [&] { host.Activates(FakeEngine::kPlayer, kChest); });
    party.Until([&] { return guest.References.at(kChest).Open; }, "the host's open state row opens the guest's chest");
    REQUIRE(host.References.at(kChest).Lid == 1.f);

    party.As(party.Host(), [&] {
        host.ScriptSetsEnabled(kChest, false);
        host.ClosesChestMenu(); // the live menu's auto-close
        host.ScriptSetsEnabled(kChest, true);
        host.Finishes3D(kChest);
    });
    party.Until([&] { return !guest.References.at(kChest).Open && !guest.References.at(kChest).Disabled; },
                "the guest has the host's close and enable");
    party.As(party.Guest(), [&] { guest.Finishes3D(kChest); });
    party.Until([&] { return host.References.at(kChest).Lid == 0.f && guest.References.at(kChest).Lid == 0.f; },
                "both lids closed, as both chests' open states read");
}
