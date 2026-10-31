#pragma once

#include <Games/Animation/ActionState.h>
#include <Games/Animation/ReplayStep.h>

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
    size_t GraphDescriptorHash = 0;
    uint64_t GraphDescriptorProjectKey = 0;
    // A copy: its owner's AI state (SkyrimBridge::SetCopyAiFields). The package (0: none or engine-made) is put back
    // in its running slot when the engine asks for a new one; scene and combat state are answered (CopyAiState.cpp).
    uint32_t OwnerPackageId = 0;
    uint32_t OwnerSceneId = 0;
    uint8_t OwnerCombatState = 0;
    // Its headtrack slots (HighProcessData::headTrackTarget, form ids): a copy's owner's, put in before the engine
    // reads them; an owner's last sent (AiStateEvents' ProcessTracking hook).
    std::array<std::atomic<uint32_t>, 6> HeadTrack{};

    // A copy's replayed action, applied at the start of its graph updates (AnimationSystem::RunReplayStep).
    using ReplayStep = AnimationReplayStep;
    ReplayStep Replay;

    // Hot reload: an extension made by an earlier load of the plugin holds strings and vectors
    // from that load's heap. It is reset (leaking them) before use, never freed or read.
    static uint32_t CurrentGeneration() noexcept;
    void EnsureCurrent() noexcept;

  private:
    uint32_t onlineFlags{0};
    // Last: written after everything else when an extension is (re)made.
    uint32_t m_generation{CurrentGeneration()};
};
