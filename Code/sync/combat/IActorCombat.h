#pragma once

#include <vector>
#include <cstdint>

namespace Payloads
{
struct TrapContact;
struct TrapHit;
} // namespace Payloads

// One of this game's actors' combat group (CombatGroup), for its AI state record.
struct CombatGroupRead
{
    bool Exists{};                 // in a combat group
    uint32_t Id{};                 // CombatGroup +0x00, unique in this game (the first is 0)
    std::vector<uint32_t> Targets; // its targets (44706), form ids
};

// A copy's owner's fight in this game's ids (SetCopyCombat): its copy holds a real controller and group with it.
struct CopyCombatFields
{
    uint8_t State{};                      // the owner's GetCombatState: 0 none, 1 fighting, 2 searching
    uint32_t TargetId{};                  // the owner's controller's target
    uint64_t GroupKey{};                  // the owner's group, unique across games; copies of one group share one
    std::vector<uint32_t> GroupTargetIds; // the owner's group's targets
};

// An actor's fight: its target, group, hits, kills and pairs. One part of IActorBridge (ports/IActorBridge.h).
struct IActorCombat
{
    virtual ~IActorCombat() = default;

    // Health damage as the engine's hits take it (attacker 0 = none): at 0 its death rules decide, with a
    // death animation and kill credit, or an essential actor bleeding out. False: not applied, a kill move holds
    // the actor (the kill move kills it).
    virtual bool DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept = 0;
    virtual uint32_t GetCombatTargetFormId(uint32_t aFormId) const noexcept = 0;
    // The game's own reaction to being attacked (fight back, flee, alarm), as when a weapon or
    // spell physically hits. Used by the owner when it applies a hit requested by another game.
    virtual void ReactToAttack(uint32_t aVictimFormId, uint32_t aAttackerFormId) noexcept = 0;
    // Who killed a dead actor (0 if alive or unknown), as Papyrus Actor.GetKiller().
    virtual uint32_t GetKillerFormId(uint32_t aFormId) const noexcept = 0;
    // A copy enters a kill move as EnterKillMove (38613) does, its killer aKillerFormId (0: the attacker's half).
    virtual void CopyEnterKillMove(uint32_t aFormId, uint32_t aKillerFormId) noexcept = 0;
    // A copy leaves a kill move without dying: its killer cleared, then EndKillMove (38614).
    virtual void CopyLeaveKillMove(uint32_t aFormId) noexcept = 0;
    // A claimed kill move (one-simulator.md, "Action claims"; kill-moves.md). An NPC taken by this game keeps the copy's deferred
    // kill (true: its clip's KillActor only counts); released, the hold ends and its counted kills are dropped.
    virtual void ActorHoldKill(uint32_t aFormId, bool aHold) noexcept = 0;
    // Its clip killed it under the hold (a counted kill, MiddleHigh +0x2F8 above 1).
    [[nodiscard]] virtual bool ActorKillCounted(uint32_t aFormId) const noexcept = 0;
    // A kill is queued and not run yet (QueueKill 37316's task; MiddleHigh +0x331): a clip's kill counts frames later.
    [[nodiscard]] virtual bool ActorKillQueued(uint32_t aFormId) const noexcept = 0;
    // The host's game kills its NPC a claimed kill move's clip killed, credited to aKillerFormId.
    virtual void KillActorBy(uint32_t aFormId, uint32_t aKillerFormId) noexcept = 0;
    // This game's actor ends its pair with a copy: the host refused its claim, or gave another the claim.
    virtual void ActorYieldPair(uint32_t aOwnFormId, uint32_t aCopyFormId) noexcept = 0;
    // The action again with no target, as the engine's own second try after a refused pair (42350); true if taken.
    virtual bool ActorPerformPlainly(uint32_t aFormId, uint32_t aActionId) noexcept = 0;
    // Alive, its graph running (kProcessMe), no player's summon: it can be lent for a pair's half.
    [[nodiscard]] virtual bool ActorCanPair(uint32_t aFormId) const noexcept = 0;
    // One of this game's actors: its combat group, for its AI state record.
    virtual CombatGroupRead GetCombatGroup(uint32_t aFormId) const noexcept = 0;
    // A copy: holds its owner's AI state.
    /// This game's actor takes a trap's hit the host's script gave its copy there.
    virtual void TakeTrapHit(uint32_t aFormId, uint32_t aTrapFormId, const Payloads::TrapHit& acHit) noexcept = 0;
    virtual void StaggerActor(uint32_t aFormId, float aMagnitude, uint32_t aHitterId) noexcept = 0;
    // A copy: holds its owner's fight in a real controller and group.
    virtual void SetCopyCombat(uint32_t aFormId, const CopyCombatFields& acFields) noexcept = 0;
};
