#include <Structs/Recipe.h>

#include <Wire/Serialization.h>

#include <algorithm>
#include <bit>
#include <tuple>

using Wire::Serialization;

namespace
{
// An effect as the recipe's identity reads it: no cost.
auto Key(const Inventory::EffectItem& acEffect) noexcept
{
    return std::tuple(acEffect.EffectId.ModId, acEffect.EffectId.BaseId, std::bit_cast<uint32_t>(acEffect.Magnitude), acEffect.Area,
                      acEffect.Duration);
}

std::vector<Inventory::EffectItem> Sorted(std::vector<Inventory::EffectItem> aEffects) noexcept
{
    std::ranges::sort(aEffects, [](const auto& acLhs, const auto& acRhs) { return Key(acLhs) < Key(acRhs); });
    return aEffects;
}
} // namespace

bool Recipe::operator==(const Recipe& acRhs) const noexcept
{
    if (IsPoison != acRhs.IsPoison || Effects.size() != acRhs.Effects.size())
        return false;
    return std::ranges::equal(Sorted(Effects), Sorted(acRhs.Effects), [](const auto& acLhs, const auto& acRhs) { return Key(acLhs) == Key(acRhs); });
}

GameId Recipe::Id() const noexcept
{
    // FNV-1a over the poison flag and the sorted effects.
    uint32_t hash = 2166136261u;
    const auto cMix = [&hash](uint32_t aValue) {
        for (int i = 0; i < 4; ++i)
        {
            hash ^= (aValue >> (i * 8)) & 0xFF;
            hash *= 16777619u;
        }
    };
    cMix(IsPoison);
    for (const auto& cEffect : Sorted(Effects))
        std::apply([&](auto... aValues) { (cMix(static_cast<uint32_t>(aValues)), ...); }, Key(cEffect));
    return GameId(kModId, hash);
}

void Recipe::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(IsPoison, 1);
    Serialization::WriteVarInt(aWriter, Effects.size());
    for (const auto& cEffect : Effects)
        cEffect.Serialize(aWriter);
}

void Recipe::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t poison = 0;
    aReader.ReadBits(poison, 1);
    IsPoison = poison != 0;
    Effects.resize(Serialization::ReadVarInt(aReader) & 0xFF);
    for (auto& effect : Effects)
        effect.Deserialize(aReader);
}
