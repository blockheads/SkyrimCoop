#include <Messages/Payloads.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace Payloads
{
void EquipmentSnapshot::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Equipment.Serialize(aWriter);
}

void EquipmentSnapshot::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Equipment.Deserialize(aReader);
}

void DrawWeapon::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteBool(aWriter, IsWeaponDrawn);
}

void DrawWeapon::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsWeaponDrawn = Serialization::ReadBool(aReader);
}

void ActorValueChanges::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);

    Serialization::WriteVarInt(aWriter, Values.size());
    for (auto& value : Values)
    {
        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
}

void ActorValueChanges::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    auto count = Serialization::ReadVarInt(aReader);
    for (int i = 0; i < count; i++)
    {
        auto key = Serialization::ReadVarInt(aReader);
        auto value = Serialization::ReadFloat(aReader);
        Values.insert({key, value});
    }
}

void ActorMaxValueChanges::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);

    Serialization::WriteVarInt(aWriter, Values.size());
    for (const auto& value : Values)
    {
        Serialization::WriteVarInt(aWriter, value.first);
        Serialization::WriteFloat(aWriter, value.second);
    }
}

void ActorMaxValueChanges::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    auto count = Serialization::ReadVarInt(aReader);
    for (decltype(count) i = 0; i < count; i++)
    {
        uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        auto value = Serialization::ReadFloat(aReader);
        Values.insert({key, value});
    }
}

void DeathStateChange::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteBool(aWriter, IsDead);
}

void DeathStateChange::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsDead = Serialization::ReadBool(aReader);
}

void HealthChange::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteFloat(aWriter, DeltaHealth);
    Serialization::WriteBool(aWriter, HasAttacker);
    Serialization::WriteVarInt(aWriter, AttackerId);
}

void HealthChange::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    DeltaHealth = Serialization::ReadFloat(aReader);
    HasAttacker = Serialization::ReadBool(aReader);
    AttackerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

void SpellCast::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, CasterId);
    SpellFormId.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, CastingSource);
    Serialization::WriteBool(aWriter, IsDualCasting);
    Serialization::WriteVarInt(aWriter, DesiredTarget);
}

void SpellCast::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    CasterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    SpellFormId.Deserialize(aReader);
    CastingSource = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsDualCasting = Serialization::ReadBool(aReader);
    DesiredTarget = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

void InterruptCast::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, CasterId);
    Serialization::WriteVarInt(aWriter, CastingSource);
}

void InterruptCast::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    CasterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CastingSource = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

void ProjectileLaunch::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ShooterID);

    Serialization::WriteFloat(aWriter, OriginX);
    Serialization::WriteFloat(aWriter, OriginY);
    Serialization::WriteFloat(aWriter, OriginZ);

    ProjectileBaseID.Serialize(aWriter);
    WeaponID.Serialize(aWriter);
    AmmoID.Serialize(aWriter);

    Serialization::WriteFloat(aWriter, ZAngle);
    Serialization::WriteFloat(aWriter, XAngle);
    Serialization::WriteFloat(aWriter, YAngle);

    ParentCellID.Serialize(aWriter);
    SpellID.Serialize(aWriter);

    Serialization::WriteVarInt(aWriter, CastingSource);

    Serialization::WriteVarInt(aWriter, Area);
    Serialization::WriteFloat(aWriter, Power);
    Serialization::WriteFloat(aWriter, Scale);

    Serialization::WriteBool(aWriter, AlwaysHit);
    Serialization::WriteBool(aWriter, NoDamageOutsideCombat);
    Serialization::WriteBool(aWriter, AutoAim);
    Serialization::WriteBool(aWriter, DeferInitialization);
    Serialization::WriteBool(aWriter, ForceConeOfFire);

    Serialization::WriteBool(aWriter, UnkBool1);
    Serialization::WriteBool(aWriter, UnkBool2);
}

void ProjectileLaunch::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    ShooterID = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    OriginX = Serialization::ReadFloat(aReader);
    OriginY = Serialization::ReadFloat(aReader);
    OriginZ = Serialization::ReadFloat(aReader);

    ProjectileBaseID.Deserialize(aReader);
    WeaponID.Deserialize(aReader);
    AmmoID.Deserialize(aReader);

    ZAngle = Serialization::ReadFloat(aReader);
    XAngle = Serialization::ReadFloat(aReader);
    YAngle = Serialization::ReadFloat(aReader);

    ParentCellID.Deserialize(aReader);
    SpellID.Deserialize(aReader);

    CastingSource = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    Area = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Power = Serialization::ReadFloat(aReader);
    Scale = Serialization::ReadFloat(aReader);

    AlwaysHit = Serialization::ReadBool(aReader);
    NoDamageOutsideCombat = Serialization::ReadBool(aReader);
    AutoAim = Serialization::ReadBool(aReader);
    DeferInitialization = Serialization::ReadBool(aReader);
    ForceConeOfFire = Serialization::ReadBool(aReader);

    UnkBool1 = Serialization::ReadBool(aReader);
    UnkBool2 = Serialization::ReadBool(aReader);
}

void AddTarget::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, TargetId);
    Serialization::WriteVarInt(aWriter, CasterId);
    SpellId.Serialize(aWriter);
    EffectId.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Magnitude);
    Serialization::WriteBool(aWriter, IsDualCasting);
    Serialization::WriteBool(aWriter, ApplyHealPerkBonus);
    Serialization::WriteBool(aWriter, ApplyStaminaPerkBonus);
}

void AddTarget::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    TargetId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CasterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    SpellId.Deserialize(aReader);
    EffectId.Deserialize(aReader);
    Magnitude = Serialization::ReadFloat(aReader);
    IsDualCasting = Serialization::ReadBool(aReader);
    ApplyHealPerkBonus = Serialization::ReadBool(aReader);
    ApplyStaminaPerkBonus = Serialization::ReadBool(aReader);
}

void RemoveSpell::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, TargetId);
    SpellId.Serialize(aWriter);
}

void RemoveSpell::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    TargetId = Serialization::ReadVarInt(aReader);
    SpellId.Deserialize(aReader);
}

void Mount::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RiderId);
    Serialization::WriteVarInt(aWriter, MountId);
}

void Mount::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    RiderId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    MountId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

void NewPackage::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    PackageId.Serialize(aWriter);
}

void NewPackage::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    PackageId.Deserialize(aReader);
}

void Dialogue::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Serialization::WriteString(aWriter, SoundFilename);
}

void Dialogue::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    SoundFilename = Serialization::ReadString(aReader);
}

void Subtitle::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Serialization::WriteString(aWriter, Text);
    Serialization::WriteVarInt(aWriter, TopicFormId);
    Serialization::WriteBool(aWriter, Force);
}

void Subtitle::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Text = Serialization::ReadString(aReader);
    TopicFormId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Force = Serialization::ReadBool(aReader);
}

void SyncExperience::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, Experience);
}

void SyncExperience::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Experience = Serialization::ReadFloat(aReader);
}

void ActiveEffects::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Serialization::WriteVarInt(aWriter, EffectIds.size());
    for (const auto& cEffectId : EffectIds)
        cEffectId.Serialize(aWriter);
}

void ActiveEffects::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    const auto cCount = Serialization::ReadVarInt(aReader);
    EffectIds.clear();
    for (uint64_t i = 0; i < cCount && i < 256; ++i)
    {
        GameId effectId;
        effectId.Deserialize(aReader);
        EffectIds.push_back(effectId);
    }
}
} // namespace Payloads
