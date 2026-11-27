#pragma once

#include <tuple>

#include <Messages/Message.h>
#include <Structs/GameId.h>
#include <Structs/Inventory.h>

struct CrimeItemOwner
{
    bool operator==(const CrimeItemOwner&) const noexcept = default;
    GameId Owner{};
    uint32_t Count{};
};

struct CrimeItem
{
    bool operator==(const CrimeItem&) const noexcept = default;
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
    bool operator==(const CrimeRequest& acRhs) const noexcept
    {
        const auto cFields = [](const CrimeRequest& acRequest) {
            return std::tie(acRequest.Sequence, acRequest.Action, acRequest.Target, acRequest.TargetActor, acRequest.Item, acRequest.Owner,
                            acRequest.Count, acRequest.GoToJail, acRequest.RemoveStolenItems, acRequest.AllowWarning, acRequest.Items);
        };
        return cFields(*this) == cFields(acRhs);
    }

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
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Faction, Violent, NonViolent, NonViolentInfamy, ViolentInfamy, PlayerEnemy); }
};

// A bounty the host's engine reported (37679): each guest shows the same HUD line.
struct CrimeReport
{
    GameId Faction{};
    uint32_t Bounty{};
    bool operator==(const CrimeReport&) const = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Faction, Bounty); }
};

// Absolute totals: retransmission cannot add bounty. A full snapshot also clears absent factions.
struct NotifyCrimeState final : MessageOf<NotifyCrimeState, kNotifyCrimeState>
{
    // Crime state rows: one per crime faction, far fewer than this.
    static constexpr uint64_t kMaxRows = 4096;
    std::vector<CrimeGold> Gold;
    std::vector<CrimeReport> Reports; // shown once, never a total
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::List(Gold, kMaxRows), Wire::List(Reports, kMaxRows));
    }
    bool operator==(const NotifyCrimeState&) const noexcept = default;
};

struct NotifyLockCrime final : MessageOf<NotifyLockCrime, kNotifyLockCrime>
{
    uint32_t Attempt{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Attempt); }
    bool operator==(const NotifyLockCrime&) const noexcept = default;
};

// A fine's (Faction: every stolen item into its chest) or a steal warning's given-back (Owner: what it owns, to To or
// ToActor), run on the player's own game: only it keeps its items' ownership (crime.md).
struct CrimeConfiscation
{
    uint32_t Actor{}; // engine bridge: form id; HostGame: session id
    GameId Faction{};
    GameId Owner{};
    GameId To{};
    uint32_t ToActor{}; // as Actor
    bool operator==(const CrimeConfiscation&) const noexcept = default;
};

struct NotifyCrimeConfiscation final : MessageOf<NotifyCrimeConfiscation, kNotifyCrimeConfiscation>
{
    uint32_t Id{};
    GameId Faction{};
    GameId Owner{};
    GameId To{};
    uint32_t ToActor{}; // session id
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, Faction, Owner, To, ToActor); }
    bool operator==(const NotifyCrimeConfiscation&) const noexcept = default;
};
