#include <Messages/Payloads.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

namespace Payloads
{
void MeleeStagger::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, HitteeId);
    Serialization::WriteVarInt(aWriter, HitterId);
    Serialization::WriteFloat(aWriter, Magnitude);
}
void MeleeStagger::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    HitteeId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HitterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Magnitude = Serialization::ReadFloat(aReader);
}

void ActorPerks::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    Content.Serialize(aWriter);
}
void ActorPerks::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Content.Deserialize(aReader);
}

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
    Serialization::WriteVarInt(aWriter, TemporaryValues.size());
    for (const auto& [cIndex, cValue] : TemporaryValues)
    {
        Serialization::WriteVarInt(aWriter, cIndex);
        Serialization::WriteFloat(aWriter, cValue);
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
    const auto cTemporaryCount = Serialization::ReadVarInt(aReader);
    for (uint64_t i = 0; i < cTemporaryCount; ++i)
    {
        const auto cIndex = Serialization::ReadVarInt(aReader);
        TemporaryValues[cIndex] = Serialization::ReadFloat(aReader);
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
    Serialization::WriteBool(aWriter, DeathActionTick.has_value());
    if (DeathActionTick)
        Serialization::WriteVarInt(aWriter, *DeathActionTick);
}

void DeathStateChange::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsDead = Serialization::ReadBool(aReader);
    DeathActionTick = Serialization::ReadBool(aReader) ? std::optional<uint64_t>{Serialization::ReadVarInt(aReader)} : std::nullopt;
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

void Dismount::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Mount::SerializePayload(aWriter);
    Whereabouts.Serialize(aWriter);
}

void Dismount::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Mount::DeserializePayload(aReader);
    Whereabouts.Deserialize(aReader);
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

void AiState::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    State.Serialize(aWriter);
}

void AiState::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    State.Deserialize(aReader);
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

void EffectFlash::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    EffectId.Serialize(aWriter);
}

void EffectFlash::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    EffectId.Deserialize(aReader);
}

void RoutedCall::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(To), 2);
    Serialization::WriteVarInt(aWriter, SessionId);
    Serialization::WriteVarInt(aWriter, Source);
    Serialization::WriteVarInt(aWriter, ReplyId);
    SerializeScriptCall(aWriter, Call);
}

void RoutedCall::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    uint64_t to = 0;
    aReader.ReadBits(to, 2);
    To = to <= static_cast<uint64_t>(CallTo::kOthers) ? static_cast<CallTo>(to) : CallTo::kOthers;
    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Source = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ReplyId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    DeserializeScriptCall(aReader, Call);
}

void CallEnded::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Source);
    Serialization::WriteVarInt(aWriter, Key);
    SerializeScriptValue(aWriter, Result);
}

void CallEnded::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Source = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (!DeserializeScriptValue(aReader, Result))
        Result = std::monostate{};
}

void PlayerNativeResult::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ReplyId);
    SerializeScriptValue(aWriter, Result);
}

void PlayerNativeResult::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    ReplyId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    if (!DeserializeScriptValue(aReader, Result))
        Result = std::monostate{};
}

} // namespace Payloads

namespace
{
// A message's bodies: at most this many (a packet carries ~30).
constexpr uint64_t kMaxBodies = 4096;
} // namespace

void Payloads::BodyFrames::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Frames.size());
    for (const auto& cFrame : Frames)
        cFrame.Serialize(aWriter);
}

void Payloads::BodyFrames::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Tick = Serialization::ReadVarInt(aReader);
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxBodies)
        return;
    Frames.resize(cCount);
    for (auto& frame : Frames)
        frame.Deserialize(aReader);
}

void Payloads::BodyRests::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Rests.size());
    for (const auto& cRest : Rests)
        cRest.Serialize(aWriter);
}

void Payloads::BodyRests::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Tick = Serialization::ReadVarInt(aReader);
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxBodies)
        return;
    Rests.resize(cCount);
    for (auto& rest : Rests)
        rest.Deserialize(aReader);
}

void Payloads::BodyPushes::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Pushes.size());
    for (const auto& cPush : Pushes)
        cPush.Serialize(aWriter);
}

void Payloads::BodyPushes::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    if (cCount > kMaxBodies)
        return;
    Pushes.resize(cCount);
    for (auto& push : Pushes)
        push.Deserialize(aReader);
}

void Payloads::PartyGold::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Gold);
    Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(Shown));
}

void Payloads::PartyGold::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Gold = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Shown = static_cast<int32_t>(static_cast<uint32_t>(Serialization::ReadVarInt(aReader)));
}

void Payloads::PartyItem::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, static_cast<uint32_t>(Shown));
}

void Payloads::PartyItem::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Item.Deserialize(aReader);
    Shown = static_cast<int32_t>(static_cast<uint32_t>(Serialization::ReadVarInt(aReader)));
}

void Payloads::PartyItems::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Items.size());
    for (const Inventory::Entry& cItem : Items)
        cItem.Serialize(aWriter);
}

void Payloads::PartyItems::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Items.resize(Serialization::ReadVarInt(aReader));
    for (Inventory::Entry& item : Items)
        item.Deserialize(aReader);
}

void Payloads::SystemQuests::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Quests.size());
    for (const GameId& cQuest : Quests)
        cQuest.Serialize(aWriter);
}

void Payloads::SystemQuests::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Quests.resize(Serialization::ReadVarInt(aReader) & 0xFFFF);
    for (GameId& quest : Quests)
        quest.Deserialize(aReader);
}

void Payloads::Recipes::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Known.size());
    for (const Recipe& cRecipe : Known)
        cRecipe.Serialize(aWriter);
}

void Payloads::Recipes::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Known.resize(Serialization::ReadVarInt(aReader) & 0xFFFF);
    for (Recipe& recipe : Known)
        recipe.Deserialize(aReader);
}

void Payloads::BugReport::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Text);
    Serialization::WriteString(aWriter, Id);
    Serialization::WriteString(aWriter, By);
    Serialization::WriteVarInt(aWriter, SessionTick);
}

void Payloads::BugReport::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Text = Serialization::ReadString(aReader);
    Id = Serialization::ReadString(aReader);
    By = Serialization::ReadString(aReader);
    SessionTick = Serialization::ReadVarInt(aReader);
}

void Payloads::DialogueHighlight::SerializePayload(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Conversation);
    Serialization::WriteVarInt(aWriter, Step);
    Serialization::WriteVarInt(aWriter, Index);
    Serialization::WriteVarInt(aWriter, PlayerId);
}

void Payloads::DialogueHighlight::DeserializePayload(Wire::Buffer::Reader& aReader) noexcept
{
    Conversation = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Step = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Index = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
