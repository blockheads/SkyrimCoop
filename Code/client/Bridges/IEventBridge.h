#pragma once

#include <cstdint>
#include <functional>

/// EventDispatcherManager::Get() sink registration abstractions.
/// Wraps RegisterSink / UnregisterSink calls so implementations (SkyrimBridge)
/// can forward to the real Skyrim EventDispatcherManager, and ReplayBridge can
/// synthesise events by calling the registered callbacks directly.
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

    // --- Replay / test injection ---
    /// Synthesise a quest start/stop event (used by ReplayBridge to replay recorded events).
    virtual void InjectQuestStartStopEvent(uint32_t aQuestFormId, bool aStarted) noexcept = 0;

    /// Synthesise a quest stage event.
    virtual void InjectQuestStageEvent(uint32_t aQuestFormId, uint32_t aNewStage) noexcept = 0;

    /// Synthesise an activate event.
    virtual void InjectActivateEvent(uint32_t aActivatorFormId, uint32_t aActivatedFormId) noexcept = 0;
};
