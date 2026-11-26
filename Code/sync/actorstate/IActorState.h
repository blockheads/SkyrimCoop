#pragma once

#include <Structs/ActorValues.h>
#include <Structs/Factions.h>
#include <Structs/Perks.h>
#include <array>
#include <cstdint>

// Structs/ types (which have no SKSE dependency).
// An actor's AI state (copy-contract.md, "AI state"): this game's form ids, 0 for none.
struct ActorAiFields
{
    bool operator==(const ActorAiFields&) const noexcept = default;

    uint32_t PackageId{};      // Actor::GetCurrentPackage
    uint32_t SceneId{};        // Actor::GetCurrentScene
    uint8_t CombatState{};     // Actor::GetCombatState
    uint32_t CombatTargetId{}; // Actor::currentCombatTarget
    // HighProcessData::headTrackTarget: what it looks at, for each reason (default, action, script, combat,
    // dialogue, procedure); 0 none. The engine looks at the highest set one (39484).
    std::array<uint32_t, 6> HeadTrackIds{};
    // The furniture it sits or sleeps in (38708), 0 none. A copy's has its graph linked as 39912 links an owner's.
    uint32_t FurnitureId{};
};

// An actor's state beside its graph (one-simulator.md, piece 2): this game's form ids, 0 for none.
struct ActorStateFields
{
    bool operator==(const ActorStateFields&) const noexcept = default;

    uint32_t Flags1{};       // ActorState flags1
    uint32_t Flags2{};       // ActorState flags2
    bool InKillMove{};       // Actor::IsInKillMove (38615)
    uint32_t KillerFormId{}; // Actor::killerHandle (+0x108), set by EnterKillMove (38613)
    bool InPair{};           // in a pair's instance (32808) or its clip (bIsSynced)
    bool Dead{};
    int32_t Noise{};         // the noise its actions make now (actionValue +0x11C, SetActionValue 37711)
};

// An actor's values, factions, state words, AI state and perks. One part of IActorBridge (ports/IActorBridge.h).
struct IActorState
{
    virtual ~IActorState() = default;

    virtual float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    virtual float GetActorTemporaryValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    // Its base value, before every modifier (ActorValueOwner::GetBaseValue).
    virtual float GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept = 0;
    // Its base made aValue (a copy's: its owner's current value).
    virtual void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept = 0;
    virtual void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept = 0;
    virtual int32_t GetActorValueCount() const noexcept = 0;
    virtual void SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept = 0;
    virtual Factions GetActorFactions(uint32_t aFormId) const noexcept = 0;
    virtual void SetActorFactions(uint32_t aFormId, const Factions& acFactions) noexcept = 0;
    virtual ActorStateFields GetActorStateFields(uint32_t aFormId) const noexcept = 0;
    // A copy: its flags as given (the caller masks them, ActorStateRules).
    virtual void SetCopyActorStateFlags(uint32_t aFormId, uint32_t aFlags1, uint32_t aFlags2) noexcept = 0;
    // A copy: its owner's noise, held until its owner's ends (ActorStateRules::CopyNoiseTimer).
    virtual void SetCopyNoise(uint32_t aFormId, int32_t aNoise) noexcept = 0;
    // One of this game's own actors, as its AI runs it.
    virtual ActorAiFields GetActorAiFields(uint32_t aFormId) const noexcept = 0;
    [[nodiscard]] virtual Perks GetPlayerPerks() const noexcept = 0;
    virtual void SetCopyPerks(uint32_t aFormId, const Perks& acPerks) noexcept = 0;
    virtual void SetCopyAiFields(uint32_t aFormId, const ActorAiFields& acFields) noexcept = 0;
};
