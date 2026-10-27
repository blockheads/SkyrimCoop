#include <Messages/CrimeMessages.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

bool CrimeRequest::IsValid() const noexcept
{
    if (!Sequence || (!Target && !TargetActor))
        return false;
    switch (Action)
    {
    case CrimeAction::Theft:
    case CrimeAction::Pickpocket:
        return Item && Count && Count <= 0x7FFFFFFFu;
    case CrimeAction::Lockpick:
        return Count != 0;
    case CrimeAction::PayFine:
        return bool(Target);
    case CrimeAction::ContainerTheft:
    {
        size_t entries = Items.size();
        if (!entries || entries > kMaxEntries)
            return false;
        for (const auto& line : Items)
        {
            entries += line.Owners.size();
            if (!line.Item.BaseId || line.Item.Count <= 0 || entries > kMaxEntries)
                return false;
            for (const auto& owner : line.Owners)
                if (!owner.Count || owner.Count > 0x7FFFu) // native ExtraCount is signed 16-bit
                    return false;
        }
        return true;
    }
    }
    return false;
}

void NotifyCrimeConfiscation::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Faction.Serialize(aWriter);
}

void NotifyCrimeConfiscation::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Id = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Faction.Deserialize(aReader);
}

void NotifyLockCrime::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Attempt);
}

void NotifyLockCrime::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Attempt = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
}

void CrimeRequest::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Sequence);
    aWriter.WriteBits(static_cast<uint8_t>(Action), 8);
    Target.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, TargetActor);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Count);
    Serialization::WriteBool(aWriter, GoToJail);
    Serialization::WriteBool(aWriter, RemoveStolenItems);
    Serialization::WriteBool(aWriter, AllowWarning);
    Serialization::WriteVarInt(aWriter, Items.size());
    for (const auto& line : Items)
    {
        line.Item.Serialize(aWriter);
        Serialization::WriteBool(aWriter, line.Taking);
        Serialization::WriteVarInt(aWriter, line.Owners.size());
        for (const auto& owner : line.Owners)
        {
            owner.Owner.Serialize(aWriter);
            Serialization::WriteVarInt(aWriter, owner.Count);
        }
    }
}

void CrimeRequest::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Sequence = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    uint64_t action{};
    aReader.ReadBits(action, 8);
    Action = static_cast<CrimeAction>(action);
    Target.Deserialize(aReader);
    TargetActor = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Item.Deserialize(aReader);
    Count = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    GoToJail = Serialization::ReadBool(aReader);
    RemoveStolenItems = Serialization::ReadBool(aReader);
    AllowWarning = Serialization::ReadBool(aReader);
    Items.clear();
    const auto count = Serialization::ReadVarInt(aReader);
    if (count > kMaxEntries)
    {
        Action = static_cast<CrimeAction>(255);
        return;
    }
    Items.resize(static_cast<size_t>(count));
    size_t entries = Items.size();
    for (auto& line : Items)
    {
        line.Item.Deserialize(aReader);
        line.Taking = Serialization::ReadBool(aReader);
        const auto owners = Serialization::ReadVarInt(aReader);
        if (owners > kMaxEntries - entries)
        {
            Action = static_cast<CrimeAction>(255);
            return;
        }
        line.Owners.resize(static_cast<size_t>(owners));
        entries += line.Owners.size();
        for (auto& owner : line.Owners)
        {
            owner.Owner.Deserialize(aReader);
            const auto quantity = Serialization::ReadVarInt(aReader);
            if (!quantity || quantity > 0x7FFFu)
            {
                Action = static_cast<CrimeAction>(255);
                return;
            }
            owner.Count = static_cast<uint32_t>(quantity);
        }
    }
}

void NotifyCrimeState::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Gold.size());
    for (const auto& row : Gold)
    {
        row.Faction.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, row.Violent);
        Serialization::WriteVarInt(aWriter, row.NonViolent);
        Serialization::WriteVarInt(aWriter, row.NonViolentInfamy);
        Serialization::WriteVarInt(aWriter, row.ViolentInfamy);
        Serialization::WriteBool(aWriter, row.PlayerEnemy);
    }
}

void NotifyCrimeState::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    Gold.clear();
    const auto count = Serialization::ReadVarInt(aReader);
    // Every row needs at least six bytes; reject an impossible count before allocating.
    if (count > 4096)
        return;
    Gold.resize(static_cast<size_t>(count));
    for (auto& row : Gold)
    {
        row.Faction.Deserialize(aReader);
        row.Violent = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
        row.NonViolent = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
        row.NonViolentInfamy = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
        row.ViolentInfamy = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
        row.PlayerEnemy = Serialization::ReadBool(aReader);
    }
}
