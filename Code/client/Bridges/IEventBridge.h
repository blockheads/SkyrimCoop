#pragma once

#include <cstdint>
#include <functional>

/// EventDispatcherManager::Get() sink registration abstractions.
/// Wraps RegisterSink / UnregisterSink calls; SkyrimBridge forwards to the real Skyrim EventDispatcherManager.
class IEventBridge
{
public:
    virtual ~IEventBridge() = default;

    // --- Quest events ---
    /// Register for TESQuestStartStopEvent.
    /// aCallback receives (questFormId, started) — true = quest started, false = stopped.
    virtual void RegisterQuestStartStopSink(std::function<void(uint32_t, bool)> aCallback) noexcept = 0;

    /// Register for TESQuestStageEvent.
    /// aCallback receives (questFormId, newStage).
    virtual void RegisterQuestStageSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept = 0;

    // --- Object / activation events ---
    /// Register for TESActivateEvent.
    /// aCallback receives (activatorFormId, activatedFormId).
    virtual void RegisterActivateSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept = 0;

    // --- Lock change events ---
    /// aCallback receives (lockFormId, isLocked).
    virtual void RegisterLockChangeSink(std::function<void(uint32_t, bool)> aCallback) noexcept = 0;

    // --- Unregister everything ---
    virtual void UnregisterAllSinks() noexcept = 0;
};
