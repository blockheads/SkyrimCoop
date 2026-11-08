#pragma once

#include <Games/Animation/ActionState.h>
#include <Games/Animation/ReplayStep.h>
#include <Games/Animation/MachineStates.h>

#include <array>
#include <atomic>

// Words: docs/design/glossary.md.
struct ActorExtension
{
    enum
    {
        kCopy = 1 << 0,   // another game owns the actor; this one shows it
        kPlayer = 1 << 1, // a player's character: this game's player, or another player's copy
    };

    bool IsCopy() const noexcept;
    bool IsLocal() const noexcept;
    bool IsPlayer() const noexcept;
    bool IsOtherPlayer() const noexcept;
    bool IsLocalPlayer() const noexcept;
    void SetCopy(bool aSet) noexcept;
    void SetPlayer(bool aSet) noexcept;

    AnimationActionState LatestAnimation;
    AnimationActionState LatestWeapEquipAnimation;
    AnimationActionState LatestPairAction; // its half of the pair it last took
    bool PairedWithCopy = false; // its last pair's partner is another game's (ReplayState::ContactKills)
    std::atomic<uint64_t> PoseHeldUntil{0}; // a copy at its clip's KillActor (ReplayState::PoseHeld); set in its graph job
    uint32_t ClashPartner = 0; // a copy: the player whose pair yielded to its own
    uint64_t ClashUntil = 0;   // its clip's blow is dropped until then (ReplayState::CancelledBlow)
    uint8_t PairStops = 0;     // a copy: pairedStops left to send after a clash
    size_t GraphDescriptorHash = 0;
    uint64_t GraphDescriptorProjectKey = 0;
    // A copy: its owner's AI state (SkyrimBridge::SetCopyAiFields). The package (0: none or engine-made) is put back
    // in its running slot when the engine asks for a new one; scene and combat state are answered (CopyAiState.cpp).
    std::atomic<uint32_t> OwnerPackageId{0};
    uint32_t OwnerSceneId = 0;
    uint8_t OwnerCombatState = 0;
    std::atomic<uint32_t> OwnerCombatTargetHandle{0};
    // Its headtrack slots (HighProcessData::headTrackTarget, form ids): a copy's owner's, put in before the engine
    // reads them; an owner's last sent (AiStateEvents' ProcessTracking hook).
    std::array<std::atomic<uint32_t>, 6> HeadTrack{};

    // A copy's owner's place, stepped to in its movement job (HookApplyMovement; set by SkyrimBridge).
    float CopyGoal[3]{};
    bool HasCopyGoal = false;

    // A copy's replayed action, applied at the start of its graph updates (AnimationSystem::RunReplayStep).
    using ReplayStep = AnimationReplayStep;
    ReplayStep Replay;
    // Its tracked state machines (MachineStates.h): an owner's states to send; a copy's owner's, to converge on.
    MachineStates::Outbox MachineOutbox;
    MachineStates::Timelines MachineTimelines;
    const void* MachineRoot = nullptr;            // its root behaviour graph last seen (NoteGraph)
    std::atomic<uint16_t> MachineGeneration{0};   // an owner's graph rebuilt this many times
    std::atomic<bool> NeedsMachineBaseline{false}; // a copy that knows none of its owner's states (CharacterService asks)
    uint64_t ReplayLag = 0;                       // how late a copy took its last replayed action (main thread)
    uint64_t ReplayTakenAt = 0;                   // its playback then

    // Hot reload: an extension made by an earlier load of the plugin holds strings and vectors
    // from that load's heap. It is reset (leaking them) before use, never freed or read.
    static uint32_t CurrentGeneration() noexcept;
    void EnsureCurrent() noexcept;

  private:
    uint32_t onlineFlags{0};
    // Last: written after everything else when an extension is (re)made.
    uint32_t m_generation{CurrentGeneration()};
};
