#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/AnnounceCharacterRequest.h>

#include <limits>

NotifyCharacterSpawn NotifyCharacterSpawn::FromAnnouncement(const AnnounceCharacterRequest& acAnnouncement, uint32_t aSessionId, uint32_t aPlayerId) noexcept
{
    const bool cIsPlayer = acAnnouncement.ReferenceId == GameId{0, 0x14};
    const bool cIsTemporary = acAnnouncement.ReferenceId.ModId == std::numeric_limits<uint32_t>::max();

    NotifyCharacterSpawn spawn;
    spawn.SessionId = aSessionId;
    // A game reference everyone has is found by its id; the player and temporary references are
    // created from their base and appearance instead.
    if (!cIsPlayer && !cIsTemporary)
        spawn.FormId = acAnnouncement.ReferenceId;
    spawn.BaseId = acAnnouncement.FormId;
    spawn.CellId = acAnnouncement.CellId;
    spawn.Position = acAnnouncement.Position;
    spawn.Rotation = acAnnouncement.Rotation;
    spawn.ChangeFlags = acAnnouncement.ChangeFlags;
    spawn.AppearanceBuffer = acAnnouncement.AppearanceBuffer;
    spawn.InventoryContent = acAnnouncement.CurrentActorData.InitialInventory;
    spawn.FactionsContent = acAnnouncement.FactionsContent;
    spawn.LatestAction = acAnnouncement.LatestAction;
    spawn.GraphState = acAnnouncement.GraphState;
    spawn.AiState = acAnnouncement.AiState;
    spawn.EffectIds = acAnnouncement.EffectIds;
    spawn.FaceTints = acAnnouncement.FaceTints;
    spawn.Weight = acAnnouncement.Weight;
    spawn.InitialActorValues = acAnnouncement.CurrentActorData.InitialActorValues;
    spawn.PlayerId = cIsPlayer ? aPlayerId : 0;
    spawn.IsDead = acAnnouncement.CurrentActorData.IsDead;
    spawn.IsPlayer = cIsPlayer;
    spawn.IsWeaponDrawn = acAnnouncement.CurrentActorData.IsWeaponDrawn;
    spawn.IsPlayerSummon = acAnnouncement.IsPlayerSummon;
    return spawn;
}

void NotifyCharacterSpawn::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, SessionId);
    FormId.Serialize(aWriter);
    BaseId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Rotation.Serialize(aWriter);
    aWriter.WriteBits(ChangeFlags, 32);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    InventoryContent.Serialize(aWriter);
    FactionsContent.Serialize(aWriter);
    LatestAction.GenerateDifferential(ActionEvent{}, aWriter);
    FaceTints.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Weight);
    InitialActorValues.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsPlayer);
    Serialization::WriteBool(aWriter, IsWeaponDrawn);
    Serialization::WriteBool(aWriter, IsPlayerSummon);
    Serialization::WriteString(aWriter, GraphState);
    AiState.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, EffectIds.size());
    for (const auto& cEffectId : EffectIds)
        cEffectId.Serialize(aWriter);
}

void NotifyCharacterSpawn::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    SessionId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FormId.Deserialize(aReader);
    BaseId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Position.Deserialize(aReader);
    Rotation.Deserialize(aReader);

    uint64_t dest = 0;
    aReader.ReadBits(dest, 32);
    ChangeFlags = dest & 0xFFFFFFFF;

    AppearanceBuffer = Serialization::ReadString(aReader);
    InventoryContent = {};
    InventoryContent.Deserialize(aReader);

    FactionsContent = {};
    FactionsContent.Deserialize(aReader);

    LatestAction = ActionEvent{};
    LatestAction.ApplyDifferential(aReader);

    FaceTints.Deserialize(aReader);
    Weight = Serialization::ReadFloat(aReader);
    InitialActorValues.Deserialize(aReader);
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    IsDead = Serialization::ReadBool(aReader);
    IsPlayer = Serialization::ReadBool(aReader);
    IsWeaponDrawn = Serialization::ReadBool(aReader);
    IsPlayerSummon = Serialization::ReadBool(aReader);
    GraphState = Serialization::ReadString(aReader);
    AiState.Deserialize(aReader);
    EffectIds.clear();
    const auto cEffects = Serialization::ReadVarInt(aReader);
    for (uint64_t i = 0; i < cEffects && i < 256; ++i)
        EffectIds.emplace_back().Deserialize(aReader);
}
