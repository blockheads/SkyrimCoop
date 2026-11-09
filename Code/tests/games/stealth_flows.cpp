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
        party.Until(
            [&] {
                const auto& known = engine.KnowledgeOfPlayer[cCopy];
                return engine.StealthEye() == DetectionRules::kUnseen && known.State &&
                       known.State->Level == DetectionRules::kUnseen;
            },
            std::format("{}'s copy answers unseen and its eye shows nobody", engine.Name),
            [&] { return Knows(engine, cCopy); });
    }
}
