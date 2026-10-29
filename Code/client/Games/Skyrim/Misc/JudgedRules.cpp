#include <Misc/JudgedRules.h>

#include <algorithm>
#include <cmath>

namespace JudgedRules
{
namespace
{
// The GetPC* functions read the player themselves; each has its own function on a reference.
uint16_t OnReference(uint16_t aFunction) noexcept
{
    switch (aFunction)
    {
    case kGetPCIsClass: return kGetIsClass;
    case kGetPCIsRace: return kGetIsRace;
    case kGetPCIsSex: return kGetIsSex;
    case kGetPCInFaction: return kGetInFaction;
    default: return 0;
    }
}
} // namespace

std::optional<Judged> For(const Item& acItem) noexcept
{
    const uint16_t cOnReference = OnReference(acItem.Function);
    if (!cOnReference && !acItem.OnPlayer && !acItem.PlayerParam[0] && !acItem.PlayerParam[1])
        return std::nullopt;
    return Judged{.Function = cOnReference ? cOnReference : acItem.Function,
                  .OnCharacter = cOnReference || acItem.OnPlayer,
                  .CharacterParam = acItem.PlayerParam};
}

float IntimidateStrength(const SpeechCheck& acCheck, const IntimidateSettings& acSettings) noexcept
{
    const float cAdvantage = std::max((acCheck.CharacterSpeech - acCheck.NpcSpeech) * 0.01f, -1.f) + 1.f;
    return std::pow(cAdvantage, acSettings.SpeechcraftCurve) * static_cast<float>(acCheck.CharacterLevel);
}

float IntimidateResistance(const SpeechCheck& acCheck, const IntimidateSettings& acSettings) noexcept
{
    const size_t cConfidence = std::min<size_t>(acCheck.NpcConfidence, acSettings.ConfidenceMults.size() - 1);
    return static_cast<float>(acCheck.NpcLevel) * acSettings.ConfidenceMults[cConfidence];
}

float BribeAmount(const SpeechCheck& acCheck, const BribeSettings& acSettings) noexcept
{
    const float cMorality = acCheck.NpcMorality * acSettings.MoralityMult + 1.f;
    const float cBase = static_cast<float>(acCheck.NpcLevel) * acSettings.NpcLevelMult + static_cast<float>(acCheck.CharacterLevel) -
                        (acCheck.CharacterSpeech - acCheck.NpcSpeech - 100.f) * acSettings.SpeechcraftMult;
    return std::pow(cBase, acSettings.CostCurve) * (acSettings.Scale * cMorality);
}

bool Compares(float aValue, uint8_t aOpCode, float aComparison) noexcept
{
    switch (aOpCode)
    {
    case 0: return aValue == aComparison;
    case 1: return aValue != aComparison;
    case 2: return aValue > aComparison;
    case 3: return aValue >= aComparison;
    case 4: return aValue < aComparison;
    case 5: return aValue <= aComparison;
    default: return false;
    }
}
} // namespace JudgedRules
