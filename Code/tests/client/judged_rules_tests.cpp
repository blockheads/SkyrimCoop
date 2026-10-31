// Conditions judged for a guest's character (JudgedFor, docs/re/systems/conditions.md): what reads the player outside
// the subject and target, and what it becomes.
#include <Misc/JudgedRules.h>

#include <catch2/catch.hpp>

using namespace JudgedRules;

namespace
{
constexpr uint16_t kGetItemCount = 47;
constexpr uint16_t kGetDetected = 45;
} // namespace

SCENARIO("A condition about the player is judged for the character", "[conditions]")
{
    THEN("one on the subject or target is left as it is: the params name the character")
    CHECK_FALSE(For({.Function = kGetItemCount}));
    THEN("one run on PlayerRef (Falion's soul gem: GetItemCount on the player) runs on the character")
    CHECK(For({.Function = kGetItemCount, .OnPlayer = true}) == Judged{.Function = kGetItemCount, .OnCharacter = true});
    THEN("a player parameter is the character (GetDetected PlayerRef on the speaker)")
    CHECK(For({.Function = kGetDetected, .PlayerParam = {true, false}}) ==
          Judged{.Function = kGetDetected, .CharacterParam = {true, false}});
    THEN("an NPC's relationship with the player is the party's (a Jarl's friend may buy a house): left as it is")
    CHECK_FALSE(For({.Function = kGetRelationshipRank, .PlayerParam = {true, false}}));
    THEN("GetPCIsSex, GetPCIsRace, GetPCIsClass and GetPCInFaction become their own function on the character")
    {
        CHECK(For({.Function = kGetPCIsSex}) == Judged{.Function = kGetIsSex, .OnCharacter = true});
        CHECK(For({.Function = kGetPCIsRace}) == Judged{.Function = kGetIsRace, .OnCharacter = true});
        CHECK(For({.Function = kGetPCIsClass}) == Judged{.Function = kGetIsClass, .OnCharacter = true});
        CHECK(For({.Function = kGetPCInFaction}) == Judged{.Function = kGetInFaction, .OnCharacter = true});
    }
}

SCENARIO("A Speech check is the character's own", "[conditions]")
{
    // Worked through Actor::WillIntimidateSucceed (37500) and Actor::GetBribeAmount (37499) by hand.
    const SpeechCheck cCheck{.CharacterSpeech = 50.f, .CharacterLevel = 10, .NpcSpeech = 30.f, .NpcLevel = 5, .NpcMorality = 1.f,
                             .NpcConfidence = 2};
    const IntimidateSettings cIntimidate{.SpeechcraftCurve = 2.f, .ConfidenceMults = {0.5f, 1.f, 2.f, 3.f, 4.f}};
    THEN("intimidation grows with the character's Speech over the NPC's, times its level")
    CHECK(IntimidateStrength(cCheck, cIntimidate) == Approx(14.4f));
    THEN("a character far below the NPC's Speech has no strength at all")
    CHECK(IntimidateStrength({.CharacterSpeech = 0.f, .NpcSpeech = 150.f}, cIntimidate) == 0.f);
    THEN("the NPC resists by its level and confidence (average)") CHECK(IntimidateResistance(cCheck, cIntimidate) == 10.f);
    THEN("a bribe costs by both levels, the Speech gap and the NPC's morality")
    {
        const BribeSettings cBribe{.MoralityMult = 0.5f, .NpcLevelMult = 2.f, .SpeechcraftMult = 0.1f, .CostCurve = 1.f, .Scale = 2.f};
        // (5 * 2 + 10) - (50 - 30 - 100) * 0.1 = 28, times 2 * (1 * 0.5 + 1)
        CHECK(BribeAmount(cCheck, cBribe) == Approx(84.f));
    }
    THEN("the condition compares the check's 0 or 1 as the item asks")
    {
        CHECK(Compares(1.f, 0, 1.f));
        CHECK_FALSE(Compares(0.f, 0, 1.f));
        CHECK(Compares(0.f, 1, 1.f));
        CHECK(Compares(1.f, 3, 1.f));
        CHECK(Compares(0.f, 4, 1.f));
    }
}
