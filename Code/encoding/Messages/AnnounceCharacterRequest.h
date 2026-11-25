#pragma once

#include "Message.h"
#include "Payloads.h"
#include <Structs/Perks.h>
#include <Structs/RecordPart.h>
#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>
#include <Structs/ActionEvent.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Rotator2_NetQuantize.h>
#include <Structs/Tints.h>
#include <Structs/Factions.h>
#include <Structs/ActorData.h>
#include <Wire/Buffer.h>

struct AnnounceCharacterRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kAnnounceCharacterRequest;

    AnnounceCharacterRequest()
        : RequestMessage(Opcode)
    {
    }

    virtual ~AnnounceCharacterRequest() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AnnounceCharacterRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Tick == acRhs.Tick && SessionId == acRhs.SessionId && ReferenceId == acRhs.ReferenceId && FormId == acRhs.FormId && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation && ChangeFlags == acRhs.ChangeFlags &&
               AppearanceBuffer == acRhs.AppearanceBuffer && LatestAction == acRhs.LatestAction && GraphState == acRhs.GraphState && Record == acRhs.Record && PerksContent == acRhs.PerksContent && EffectIds == acRhs.EffectIds && HeldEffects == acRhs.HeldEffects && FaceTints == acRhs.FaceTints && Weight == acRhs.Weight && IsDragon == acRhs.IsDragon && IsMount == acRhs.IsMount && CommanderId == acRhs.CommanderId;
    }

    // The id the host gave it: the player's from the join answer, the host's actors' from HostCharacters.
    uint64_t Tick{}; // NotifyCharacterSpawn::Tick
    uint32_t SessionId{};
    GameId ReferenceId{};
    GameId FormId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    Rotator2_NetQuantize Rotation{};
    uint32_t ChangeFlags{};
    std::string AppearanceBuffer{};
    // What its game shows for it (GetDisplayFullName): the copy's name, a player's too.
    ActionEvent LatestAction{};
    // The behaviour graphs' state, as a savegame keeps it: what a copy's fresh graph loads.
    std::string GraphState{};
    // Its record's parts as its owner last declared them (CopyRecords): the copy holds them from the start.
    RecordParts Record{};
    Perks PerksContent;
    // The effects showing on it, for the copy to play from the start (EffectVisualsService).
    std::vector<GameId> EffectIds{};
    // The effects it holds, (spell, effect): the copy answers scripts and conditions with them (OwnerEffects).
    std::vector<struct Payloads::ActiveEffects::Held> HeldEffects{};
    Tints FaceTints{};
    float Weight{50.f}; // the base's (TESNPC weight): the engine saves it with the actor, not in AppearanceBuffer
    bool IsDragon{};
    bool IsMount{};
    uint32_t CommanderId{}; // a summon's caster, by session id (0: none)
    ActorData CurrentActorData{};
};
