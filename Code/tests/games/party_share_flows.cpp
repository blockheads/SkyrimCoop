// The party is the Dragonborn (docs/design/dragonborn.md): words of power, dragon souls and the story's factions and
// spells a player takes reach every player, whichever game's engine wrote them; a player's own moment stays theirs.
#include "Party.h"

namespace
{
constexpr uint32_t kFus = 0x13E22;           // WordFus
constexpr uint32_t kRo = 0x13E23;            // WordRo
constexpr uint32_t kHarbinger = 0x1070DD;    // CompanionsHarbingerFaction (a title's)
constexpr uint32_t kWerewolfFaction = 0x91822; // PlayerWerewolfFaction (a beast form's)
constexpr uint32_t kBeastForm = 0x92C48;     // the Companions' Beast Form power
constexpr uint32_t kWerewolfQuest = 0x2BA16; // PlayerWerewolfQuest, started by the beast form's effect
constexpr uint32_t kSmoke = 0x30A4F;          // QASmoke

void Plugins(FakeEngine& aEngine)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = kSmoke;
    aEngine.LoadCell(kSmoke);
}

// That game's copy of a player of another game (the first, with aSkip copies passed).
uint32_t PlayerCopy(const FakeEngine& acEngine, size_t aSkip = 0)
{
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player && aSkip-- == 0)
            return cId;
    return 0;
}

float SoulsOf(FakeEngine& aEngine)
{
    return aEngine.Souls[FakeEngine::kPlayer];
}

bool InFaction(FakeEngine& aEngine, uint32_t aFaction)
{
    return aEngine.Factions[FakeEngine::kPlayer].contains(aFaction);
}

bool HasSpell(FakeEngine& aEngine, uint32_t aSpell)
{
    return aEngine.AddedSpells[FakeEngine::kPlayer].contains(aSpell);
}
} // namespace

SCENARIO("A word of power any game's player learns, every player knows", "[dragonborn]")
{
    Party party(2, Plugins);
    World& host = party.Host();
    World& guest = party.Guest(0);
    World& other = party.Guest(1);
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    auto& otherEngine = Party::Engine(other);

    WHEN("the host's own script teaches its player a word (no player's action behind it)")
    {
        party.As(host, [&] { hostEngine.TeachesWord(kFus); });
        party.Until([&] { return guestEngine.WordsKnown.contains(kFus) && otherEngine.WordsKnown.contains(kFus); },
                    "both guests' players learn it");
        THEN("each game learned it once, as the engine's own TeachWord (its HUD)")
        {
            CHECK(hostEngine.WordsLearnedShown == 1);
            CHECK(guestEngine.WordsLearnedShown == 1);
            CHECK(otherEngine.WordsLearnedShown == 1);
        }
    }

    WHEN("a guest reads a word wall: the host's wall script runs Game.TeachWord for the guest's character")
    {
        party.Until([&] { return PlayerCopy(hostEngine) != 0; }, "the host has the guest's character");
        const uint32_t cReader = PlayerCopy(hostEngine);
        party.As(host, [&] { hostEngine.TeachesWord(kRo, cReader); });
        party.Until([&] { return guestEngine.WordsKnown.contains(kRo) && otherEngine.WordsKnown.contains(kRo); },
                    "the reader's and the other guest's players learn it");
        party.Frame();
        party.Frame();
        THEN("it comes back to nobody: each game showed it once")
        {
            CHECK(hostEngine.WordsLearnedShown == 1);
            CHECK(guestEngine.WordsLearnedShown == 1);
            CHECK(otherEngine.WordsLearnedShown == 1);
        }
    }

    WHEN("a script unlocks a word (Paarthurnax's Game.UnlockWord)")
    {
        party.As(host, [&] { hostEngine.UnlocksWord(kFus); });
        party.Until([&] { return guestEngine.WordsUnlocked.contains(kFus) && otherEngine.WordsUnlocked.contains(kFus); },
                    "every guest's player has it unlocked");
    }
}

SCENARIO("A dragon's soul reaches every player, whoever killed it; each spends its own", "[dragonborn]")
{
    Party party(2, Plugins);
    World& host = party.Host();
    World& guest = party.Guest(0);
    World& other = party.Guest(1);
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    auto& otherEngine = Party::Engine(other);
    party.Until([&] { return PlayerCopy(hostEngine, 1) != 0; }, "the host has both guests' characters");
    party.Agree("the games agree on the players");

    WHEN("the guest kills a dragon: on the host, its death script absorbs the soul into the guest's character")
    {
        // MQKillDragonScript.DeathSequence, run for the guest (rule 2): AbsorbActor.ModActorValue("DragonSouls", 1).
        const uint32_t copy = PlayerCopy(hostEngine);
        party.As(host, [&] { hostEngine.ModifiesSouls(copy, 1.f, copy); });
        party.Until([&] { return SoulsOf(guestEngine) == 1.f && SoulsOf(otherEngine) == 1.f; }, "both guests' players gain the soul");

        THEN("every player has one soul, each shown it as the engine's own gain; the copy's is shown nowhere")
        {
            CHECK(SoulsOf(hostEngine) == 1.f);
            CHECK(hostEngine.SoulsAbsorbedShown == 1);
            CHECK(guestEngine.SoulsAbsorbedShown == 1);
            CHECK(otherEngine.SoulsAbsorbedShown == 1);
        }

        AND_WHEN("the host's script teaches a word and the guest spends its soul on it in the Magic menu")
        {
            party.As(host, [&] { hostEngine.TeachesWord(kFus); });
            party.Until([&] { return guestEngine.WordsKnown.contains(kFus); }, "the guest learns the word");
            party.As(guest, [&] { guestEngine.SpendsSoul(kFus); });
            party.Frame();
            party.Frame();
            party.Frame();

            THEN("the soul and the unlock are the guest's own")
            {
                CHECK(SoulsOf(guestEngine) == 0.f);
                CHECK(guestEngine.WordsUnlocked.contains(kFus));
                CHECK(SoulsOf(hostEngine) == 1.f);
                CHECK(SoulsOf(otherEngine) == 1.f);
                CHECK(!hostEngine.WordsUnlocked.contains(kFus));
                CHECK(!otherEngine.WordsUnlocked.contains(kFus));
            }
        }
    }

    WHEN("the host kills one")
    {
        party.As(host, [&] { hostEngine.ModifiesSouls(FakeEngine::kPlayer, 1.f, FakeEngine::kPlayer); });
        party.Until([&] { return SoulsOf(guestEngine) == 1.f && SoulsOf(otherEngine) == 1.f; }, "both guests' players gain the soul");
        THEN("the host's player has it once") CHECK(SoulsOf(hostEngine) == 1.f);
    }
}

SCENARIO("The story's titles and beast blood reach every player; a player's beast form stays theirs", "[dragonborn]")
{
    Party party(1, Plugins);
    World& host = party.Host();
    World& guest = party.Guest();
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    party.Until([&] { return PlayerCopy(hostEngine) != 0; }, "the host has the guest's character");
    const uint32_t cCopy = PlayerCopy(hostEngine);

    WHEN("the guest's picked line sets the Companions' last stage: its fragment makes the guest's character Harbinger")
    {
        // C06's fragment, run for the guest as the story: Game.GetPlayer().AddToFaction(HarbingerFaction).
        party.As(host, [&] { hostEngine.SetsFaction(cCopy, kHarbinger, 0, cCopy, true); });
        party.Until([&] { return InFaction(guestEngine, kHarbinger); }, "the guest's player is Harbinger");
        THEN("the host's player is too: the title is the party's") CHECK(InFaction(hostEngine, kHarbinger));
    }

    WHEN("the story gives the host's player Beast Form (C03's rampage, no player's action behind it)")
    {
        party.As(host, [&] { hostEngine.AddsSpell(FakeEngine::kPlayer, kBeastForm, true); });
        party.Until([&] { return HasSpell(guestEngine, kBeastForm); }, "the guest's player can transform too");

        AND_WHEN("the host's player transforms: its body's quest makes it a werewolf for the night")
        {
            // WerewolfChangeEffectScript (on the player) starts PlayerWerewolfQuest; WerewolfTransformVisual sets its
            // stage 1, whose fragment adds the factions: the story of that player's own moment.
            hostEngine.BodyQuests.insert(kWerewolfQuest);
            party.As(host, [&] {
                hostEngine.SetsFaction(FakeEngine::kPlayer, kWerewolfFaction, 0, FakeEngine::kPlayer, true, kWerewolfQuest);
            });
            party.Frame();
            party.Frame();
            party.Frame();
            THEN("the guest's player stays itself") CHECK(!InFaction(guestEngine, kWerewolfFaction));
        }
    }

    WHEN("the guest's player's own script (a lever, its own action) adds it to a faction on the host")
    {
        party.As(host, [&] { hostEngine.SetsFaction(cCopy, kHarbinger, 0, cCopy, false); });
        party.Frame();
        party.Frame();
        party.Frame();
        THEN("it is not the party's") CHECK(!InFaction(hostEngine, kHarbinger));
    }
}

SCENARIO("A dragon dissolving shows on every game, though the guest's kill started it", "[dragonborn][cutscene]")
{
    constexpr uint32_t kDragon = 0x1CA03;          // EncDragon01Fire
    constexpr uint32_t kAbsorbEffect = 0x280C0;    // a VisualEffect (DragonAbsorbEffect's kind)
    Party party(1, Plugins);
    World& host = party.Host();
    World& guest = party.Guest();
    auto& hostEngine = Party::Engine(host);
    auto& guestEngine = Party::Engine(guest);
    uint32_t dragon = 0;
    party.As(host, [&] { dragon = hostEngine.LoadNpc(kDragon, kSmoke, {300.f, 0.f, 0.f}); });
    party.Until([&] { return guestEngine.CopiesOf(kDragon).size() == 1 && PlayerCopy(hostEngine) != 0; }, "each game has the other's actors");
    const uint32_t cKiller = PlayerCopy(hostEngine);
    const uint32_t cDragonCopy = guestEngine.CopiesOf(kDragon)[0];

    WHEN("MQKillDragonScript.DeathSequence, run for the guest (its kill: a death is the story), plays the dragon's "
         "dissolve and the absorb into the killer")
    {
        LocalCall skin{.Class = "Actor", .Function = "PlaySubGraphAnimation", .Self = LocalForm{dragon}, .Arguments = {std::string("SkinFadeOut")}};
        LocalCall absorb{.Class = "VisualEffect", .Function = "Play", .Self = LocalForm{kAbsorbEffect},
                         .Arguments = {LocalForm{dragon}, 8.f, LocalForm{cKiller}}};
        LocalCall absorbMan{.Class = "VisualEffect", .Function = "Play", .Self = LocalForm{kAbsorbEffect + 1},
                            .Arguments = {LocalForm{cKiller}, 8.f, LocalForm{dragon}}};
        party.As(host, [&] {
            hostEngine.RunScript(skin, cKiller, true);
            hostEngine.RunScript(absorb, cKiller, true);
            hostEngine.RunScript(absorbMan, cKiller, true);
        });
        party.Until([&] { return guestEngine.Shows[cDragonCopy].size() == 2 && guestEngine.Shows[FakeEngine::kPlayer].size() == 1; },
                    "the guest's copy of the dragon shows both, its player the absorb");
        party.Frame();
        party.Frame();
        THEN("each game ran each once: the dragon on its copy, the absorb on each game's own player")
        {
            CHECK(hostEngine.Shows[dragon] == std::vector<std::string>{"Actor.PlaySubGraphAnimation", "VisualEffect.Play"});
            CHECK(guestEngine.Shows[cDragonCopy] == std::vector<std::string>{"Actor.PlaySubGraphAnimation", "VisualEffect.Play"});
            CHECK(guestEngine.Shows[FakeEngine::kPlayer] == std::vector<std::string>{"VisualEffect.Play"});
            CHECK(hostEngine.Shows[cKiller] == std::vector<std::string>{"VisualEffect.Play"});
            CHECK(guestEngine.Shows[PlayerCopy(guestEngine)].empty());
        }
    }
}
