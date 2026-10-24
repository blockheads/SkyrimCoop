#pragma once

#include "Message.h"
#include <Structs/ActorAiState.h>
#include <Structs/GameId.h>
#include <Structs/ActionEvent.h>
#include <Structs/Tints.h>
#include <Structs/Inventory.h>
#include <Structs/Factions.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Rotator2_NetQuantize.h>
#include <Structs/ActorValues.h>
#include <Wire/Buffer.h>

struct AnnounceCharacterRequest;

struct NotifyCharacterSpawn final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyCharacterSpawn;

    NotifyCharacterSpawn()
        : NotifyMessage(Opcode)
    {
    }

    // What others spawn for a character its owner announced (aPlayerId: the owner's, for players).
    static NotifyCharacterSpawn FromAnnouncement(const AnnounceCharacterRequest& acAnnouncement, uint32_t aSessionId, uint32_t aPlayerId) noexcept;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCharacterSpawn& acRhs) const noexcept
    {
        return SessionId == acRhs.SessionId && FormId == acRhs.FormId && BaseId == acRhs.BaseId && CellId == acRhs.CellId && Position == acRhs.Position && Rotation == acRhs.Rotation && ChangeFlags == acRhs.ChangeFlags && AppearanceBuffer == acRhs.AppearanceBuffer &&
               InventoryContent == acRhs.InventoryContent && FactionsContent == acRhs.FactionsContent && FaceTints == acRhs.FaceTints && PlayerId == acRhs.PlayerId && IsDead == acRhs.IsDead && IsPlayer == acRhs.IsPlayer && IsWeaponDrawn == acRhs.IsWeaponDrawn &&
               IsPlayerSummon == acRhs.IsPlayerSummon && GraphState == acRhs.GraphState && AiState == acRhs.AiState && EffectIds == acRhs.EffectIds && GetOpcode() == acRhs.GetOpcode();
    }

    uint32_t SessionId{};
    GameId FormId{};
    GameId BaseId{};
    GameId CellId{};
    Vector3_NetQuantize Position{};
    Rotator2_NetQuantize Rotation{};
    uint32_t ChangeFlags{};
    std::string AppearanceBuffer{};
    Inventory InventoryContent{};
    Factions FactionsContent{};
    ActionEvent LatestAction{};
    // The owner's behaviour graphs' state: the copy's fresh graph loads it instead of replaying LatestAction.
    std::string GraphState{};
    // What its AI is doing, for the copy to hold from the start (AiStateService).
    ActorAiState AiState{};
    // The effects showing on it, for the copy to play from the start (EffectVisualsService).
    std::vector<GameId> EffectIds{};
    Tints FaceTints{};
    ActorValues InitialActorValues{};
    uint32_t PlayerId{};
    bool IsDead{};
    bool IsPlayer{};
    bool IsWeaponDrawn{};
    bool IsPlayerSummon{};
};
