#pragma once

#include "Message.h"
#include <Structs/ActorAiState.h>
#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>
#include <Structs/ActionEvent.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Rotator2_NetQuantize.h>
#include <Structs/Tints.h>
#include <Structs/Factions.h>
#include <Structs/ActorData.h>
#include <Wire/Buffer.h>

struct AssignCharacterRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kAssignCharacterRequest;

    AssignCharacterRequest()
        : RequestMessage(Opcode)
    {
    }

    virtual ~AssignCharacterRequest() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AssignCharacterRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && SessionId == acRhs.SessionId && ReferenceId == acRhs.ReferenceId && FormId == acRhs.FormId && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && ChangeFlags == acRhs.ChangeFlags &&
               AppearanceBuffer == acRhs.AppearanceBuffer && FactionsContent == acRhs.FactionsContent && LatestAction == acRhs.LatestAction && GraphState == acRhs.GraphState && AiState == acRhs.AiState && EffectIds == acRhs.EffectIds && FaceTints == acRhs.FaceTints && IsDragon == acRhs.IsDragon && IsMount == acRhs.IsMount && IsPlayerSummon == acRhs.IsPlayerSummon;
    }

    // The id the host gave it: the player's from the join answer, the host's actors' from HostCharacters.
    uint32_t SessionId{};
    GameId ReferenceId{};
    GameId FormId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    Rotator2_NetQuantize Rotation{};
    uint32_t ChangeFlags{};
    std::string AppearanceBuffer{};
    Factions FactionsContent{};
    ActionEvent LatestAction{};
    // The behaviour graphs' state, as a savegame keeps it: what a copy's fresh graph loads.
    std::string GraphState{};
    // What its AI is doing, for the copy to hold from the start (AiStateService).
    ActorAiState AiState{};
    // The effects showing on it, for the copy to play from the start (EffectVisualsService).
    std::vector<GameId> EffectIds{};
    Tints FaceTints{};
    bool IsDragon{};
    bool IsMount{};
    bool IsPlayerSummon{};
    ActorData CurrentActorData{};
};
