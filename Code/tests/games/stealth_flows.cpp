// The stealth eye (docs/re/systems/stealth.md): another player's copy is never a witness of this game's player.
#include "Fixtures.h"
#include "Party.h"

namespace
{
std::string Knows(FakeEngine& aEngine, uint32_t aWitness)
{
    const auto& known = aEngine.KnowledgeOfPlayer[aWitness];
    return std::format("{}: eye {}, its copy's state {}, {} frames old, queued {}", aEngine.Name, aEngine.StealthEye(),
                       known.State ? std::to_string(known.State->Level) : "none",
                       aEngine.DetectionFrame - known.UpdatedAt, known.Queued);
}
} // namespace

SCENARIO("The other player looking at a sneaking player: its copy never raises the eye", "[stealth][copies]")
{
    Party party(1, GameTest::SmokeWorld);
    for (World* pGame : {&party.Host(), &party.Guest()})
    {
        FakeEngine& engine = Party::Engine(*pGame);
        party.Until([&] { return GameTest::PlayerCopy(engine) != 0; },
                    std::format("{} shows the other player", engine.Name));
        const uint32_t cCopy = GameTest::PlayerCopy(engine);

        // It faces the player, and judged it seen in a pass before (Skyrim kept that level at any age, 42702).
        engine.Sees[cCopy] = 50;
        engine.JudgedPlayer(cCopy, 50);
        REQUIRE(engine.StealthEye() == DetectionRules::kUnseen);
        // A crime check asks the copy itself (37764): its next pass answers unseen.
        party.Until(
            [&] {
                const auto* cpState = engine.FreshState(cCopy);
                return engine.StealthEye() == DetectionRules::kUnseen && cpState &&
                       cpState->Level == DetectionRules::kUnseen;
            },
            std::format("{}'s copy answers unseen and its eye shows nobody", engine.Name),
            [&] { return Knows(engine, cCopy); });
    }
}

SCENARIO("The player talks to the other player's copy: its forced detection never shows on the eye",
         "[stealth][copies]")
{
    Party party(1, GameTest::SmokeWorld);
    for (World* pGame : {&party.Host(), &party.Guest()})
    {
        FakeEngine& engine = Party::Engine(*pGame);
        party.Until([&] { return GameTest::PlayerCopy(engine) != 0; },
                    std::format("{} shows the other player", engine.Name));
        const uint32_t cCopy = GameTest::PlayerCopy(engine);
        party.Until([&] { return engine.FreshState(cCopy) != nullptr; },
                    std::format("{}'s copy is asked about its player", engine.Name));

        // Skyrim: 37765 sets 100 without a detection pass; 41408 skips an actor with kDoNotShowOnStealthMeter.
        engine.ForcesDetected(cCopy);
        for (int frame = 0; frame < 180; ++frame)
        {
            INFO(std::format("frame {}: {}", frame, Knows(engine, cCopy)));
            REQUIRE(engine.StealthEye() == DetectionRules::kUnseen);
            party.Frame();
        }
    }
}

SCENARIO("A player's copy keeps its owner's look at this game's player, whom it never detects", "[stealth][copies][headtrack]")
{
    // Skyrim (duo, guest-rides-host-horse, 2026-11-10): "head_track: client (owner) ['session 1'] vs host copy []".
    // The guest's player looked at the host's; its copy on the host got that slot and lost it every frame.
    // ProcessTracking (38009) drops a look at this game's player its tracker does not detect, and a player's copy
    // holds an unseen state of that player (DetectionRules::Installed). A copy's look is not its own to judge.
    Party party(1, GameTest::SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    FakeEngine& guest = Party::Engine(party.Guest());
    party.Until([&] { return GameTest::PlayerCopy(host) != 0 && GameTest::PlayerCopy(guest) != 0; }, "each game shows the other player");
    const uint32_t cGuestOnHost = GameTest::PlayerCopy(host);
    const auto cLook = [&] { return host.GetActorAiFields(cGuestOnHost).HeadTrackIds[0]; };

    party.As(party.Guest(), [&] { guest.LooksAt(FakeEngine::kPlayer, 0, GameTest::PlayerCopy(guest)); });
    party.Until([&] { return cLook() == FakeEngine::kPlayer; }, "the copy on the host looks at the host's player");
    // Its own detection of that player is asked, queued, and answered unseen within these frames.
    for (int frame = 0; frame < 180; ++frame)
    {
        INFO(std::format("frame {}: {}", frame, Knows(host, cGuestOnHost)));
        REQUIRE(cLook() == FakeEngine::kPlayer);
        party.Frame();
    }
    party.Until([&] { return host.FreshState(cGuestOnHost) != nullptr; }, "the copy holds a state of the host's player");
    CHECK(host.FreshState(cGuestOnHost)->Level == DetectionRules::kUnseen);
    for (int frame = 0; frame < 30; ++frame)
        party.Frame();
    CHECK(cLook() == FakeEngine::kPlayer);
}

SCENARIO("An NPC that does not detect its game's player drops its default look at that player", "[stealth][headtrack]")
{
    // ProcessTracking (38009), read in IDA: target the player's handle, a fresh state of level 0 or less, slot 0 cleared.
    Party party(1, GameTest::SmokeWorld);
    FakeEngine& host = Party::Engine(party.Host());
    uint32_t npc = 0;
    party.As(party.Host(), [&] { npc = host.LoadNpc(0x0001A67C, 0x00031D2E, {30.f, 40.f, 0.f}); });
    const auto cLook = [&] { return host.GetActorAiFields(npc).HeadTrackIds[0]; };
    party.As(party.Host(), [&] {
        host.Sees[npc] = 50;
        host.LooksAt(npc, 0, FakeEngine::kPlayer);
    });
    for (int frame = 0; frame < 30; ++frame)
        party.Frame();
    CHECK(cLook() == FakeEngine::kPlayer); // detected: it keeps looking
    party.As(party.Host(), [&] { host.Sees[npc] = DetectionRules::kUnseen; });
    party.Until([&] { return cLook() == 0; }, "it looks away once its state of the player is unseen");
}
