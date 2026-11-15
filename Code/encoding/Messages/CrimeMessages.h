#pragma once

#include <Messages/Message.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>

struct CrimeItemOwner
{
    GameId Owner{};
    uint32_t Count{};
};

struct CrimeItem
{
    Inventory::Entry Item;
    std::vector<CrimeItemOwner> Owners;
    bool Taking{};
};

// Actions, never a guest-computed fine. The host resolves the actor and judges the action.
enum class CrimeAction : uint8_t
{
    Theft,
    Pickpocket,
    Lockpick,
    ContainerTheft,
    PayFine
};

struct CrimeRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kCrimeRequest;
    static constexpr size_t kMaxEntries = 4096; // item lines and ownership stacks combined
    CrimeRequest() : RequestMessage(Opcode)
    {
    }
    void SerializeRaw(Wire::Buffer::Writer&) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader&) noexcept override;
    bool IsValid() const noexcept;

    uint32_t Sequence{};
    CrimeAction Action{};
    GameId Target{};
    uint32_t TargetActor{}; // session id instead of Target for a copied actor
    GameId Item{};
    GameId Owner{}; // theft: the item's owner StealAlarm was given (a created item's target is its owner actor)
    uint32_t Count{}; // theft quantity; lockpick menu attempt id
    bool GoToJail{}; // native PayFine: move to the release marker, not a jail sentence
    bool RemoveStolenItems{};
    bool AllowWarning{};
    std::vector<CrimeItem> Items; // container close: native item actions, never the menu's money total
};

struct CrimeGold
{
    GameId Faction{};
    uint32_t Violent{};
    uint32_t NonViolent{};
    uint32_t NonViolentInfamy{};
    uint32_t ViolentInfamy{};
    bool PlayerEnemy{}; // the faction's native crime hostility flag, including zero-bounty hostility
    bool operator==(const CrimeGold&) const = default;
};

// A bounty the host's engine reported (37679): each guest shows the same HUD line.
struct CrimeReport
{
    GameId Faction{};
    uint32_t Bounty{};
    bool operator==(const CrimeReport&) const = default;
};

// Absolute totals: retransmission cannot add bounty. A full snapshot also clears absent factions.
struct NotifyCrimeState final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyCrimeState;
    NotifyCrimeState() : NotifyMessage(Opcode)
    {
    }
    void SerializeRaw(Wire::Buffer::Writer&) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader&) noexcept override;
    std::vector<CrimeGold> Gold;
    std::vector<CrimeReport> Reports; // shown once, never a total
};

struct NotifyLockCrime final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyLockCrime;
    NotifyLockCrime() : NotifyMessage(Opcode)
    {
    }
    void SerializeRaw(Wire::Buffer::Writer&) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader&) noexcept override;
    uint32_t Attempt{};
};

struct CrimeConfiscation
{
    uint32_t Actor{}; // engine bridge: form id; HostGame: session id
    GameId Faction{};
};

struct NotifyCrimeConfiscation final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyCrimeConfiscation;
    NotifyCrimeConfiscation() : NotifyMessage(Opcode)
    {
    }
    void SerializeRaw(Wire::Buffer::Writer&) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader&) noexcept override;
    uint32_t Id{};
    GameId Faction{};
};
