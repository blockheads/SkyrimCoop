#pragma once

#include <Structs/ActionEvent.h>

struct ActorExtension
{
    enum
    {
        kRemote = 1 << 0,
        kPlayer = 1 << 1,
    };

    bool IsRemote() const noexcept;
    bool IsLocal() const noexcept;
    bool IsPlayer() const noexcept;
    bool IsRemotePlayer() const noexcept;
    bool IsLocalPlayer() const noexcept;
    void SetRemote(bool aSet) noexcept;
    void SetPlayer(bool aSet) noexcept;

    ActionEvent LatestAnimation{};
    ActionEvent LatestWeapEquipAnimation{};
    size_t GraphDescriptorHash = 0;

    // A copy's replayed action, applied at the start of its graph updates (AnimationSystem::RunReplayStep).
    struct ReplayStep
    {
        enum : uint8_t
        {
            kIdle,
            kVariables, // staged by our update
            kAction,    // variables in, one graph update to go
            kBusy,      // being applied
            kTaken,
            kRefused,
        };
        std::atomic<uint8_t> Phase{kIdle};
        uint8_t Retries = 0; // graph updates the action was refused in
        ActionEvent Action{};
        AnimationVariables Variables{};
    };
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
