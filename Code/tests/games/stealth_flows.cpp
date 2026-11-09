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
