#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/AssignCharacterRequest.h>

#include <limits>

NotifyCharacterSpawn NotifyCharacterSpawn::FromAssignment(const AssignCharacterRequest& acAssignment, uint32_t aSessionId, uint32_t aPlayerId) noexcept
{
    const bool cIsPlayer = acAssignment.ReferenceId == GameId{0, 0x14};
    const bool cIsTemporary = acAssignment.ReferenceId.ModId == std::numeric_limits<uint32_t>::max();

    NotifyCharacterSpawn spawn;
    spawn.SessionId = aSessionId;
    // A game reference everyone has is found by its id; the player and temporary references are
    // created from their base and appearance instead.
    if (!cIsPlayer && !cIsTemporary)
        spawn.FormId = acAssignment.ReferenceId;
    spawn.BaseId = acAssignment.FormId;
    spawn.CellId = acAssignment.CellId;
    spawn.Position = acAssignment.Position;
    spawn.Rotation = acAssignment.Rotation;
    spawn.ChangeFlags = acAssignment.ChangeFlags;
    spawn.AppearanceBuffer = acAssignment.AppearanceBuffer;
    spawn.InventoryContent = acAssignment.CurrentActorData.InitialInventory;
    spawn.FactionsContent = acAssignment.FactionsContent;
    spawn.LatestAction = acAssignment.LatestAction;
    spawn.GraphState = acAssignment.GraphState;
    spawn.AiState = acAssignment.AiState;
    spawn.FaceTints = acAssignment.FaceTints;
    spawn.InitialActorValues = acAssignment.CurrentActorData.InitialActorValues;
    spawn.PlayerId = cIsPlayer ? aPlayerId : 0;
    spawn.IsDead = acAssignment.CurrentActorData.IsDead;
    spawn.IsPlayer = cIsPlayer;
    spawn.IsWeaponDrawn = acAssignment.CurrentActorData.IsWeaponDrawn;
    spawn.IsPlayerSummon = acAssignment.IsPlayerSummon;
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
    InitialActorValues.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsPlayer);
    Serialization::WriteBool(aWriter, IsWeaponDrawn);
    Serialization::WriteBool(aWriter, IsPlayerSummon);
    Serialization::WriteString(aWriter, GraphState);
    AiState.Serialize(aWriter);
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
    InitialActorValues.Deserialize(aReader);
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    IsDead = Serialization::ReadBool(aReader);
    IsPlayer = Serialization::ReadBool(aReader);
    IsWeaponDrawn = Serialization::ReadBool(aReader);
    IsPlayerSummon = Serialization::ReadBool(aReader);
    GraphState = Serialization::ReadString(aReader);
    AiState.Deserialize(aReader);
}
