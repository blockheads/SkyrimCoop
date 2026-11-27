#include <Structs/Factions.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace
{
constexpr uint64_t kMaxFactions = 0x1FF;
} // namespace

bool Factions::operator==(const Factions& acRhs) const noexcept
{
    return NpcFactions == acRhs.NpcFactions && ExtraFactions == acRhs.ExtraFactions;
}

bool Factions::operator!=(const Factions& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void Factions::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    // Limit the number of factions to send
    Serialization::WriteVarInt(aWriter, NpcFactions.size() & 0x1FF);

    for (auto& entry : NpcFactions)
    {
        entry.Serialize(aWriter);
    }

    Serialization::WriteVarInt(aWriter, ExtraFactions.size() & 0x1FF);

    for (auto& entry : ExtraFactions)
    {
        entry.Serialize(aWriter);
    }
}

void Factions::Deserialize(Wire::Buffer::Reader& aReader)
{
    NpcFactions.resize(Serialization::ReadCount(aReader, kMaxFactions));
    for (auto& entry : NpcFactions)
    {
        entry.Deserialize(aReader);
    }

    ExtraFactions.resize(Serialization::ReadCount(aReader, kMaxFactions));
    for (auto& entry : ExtraFactions)
    {
        entry.Deserialize(aReader);
    }
}
