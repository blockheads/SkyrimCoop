#pragma once

#include "FakeEngine.h"
#include <Bridges/IWaitBridge.h>
#include <DateTime.h>
#include <Events/MenuEvent.h>
#include <World.h>

// Native menu behavior: an opened menu pauses, the host's timer takes a step, guests need an explicit replay.
struct FakeWait final : IWaitBridge
{
    explicit FakeWait(FakeEngine& aEngine) : Engine(aEngine)
    {
    }
    FakeEngine& Engine;
    Callbacks Hooks;
    bool Available = true;
    bool Loaded = true;
    bool Open{};
    bool Running{};
    bool Sleeping{};
    bool Driver{};
    uint32_t Hours{};
    uint32_t Steps{};
    uint32_t RemainingSeconds{};
    uint32_t TickSeconds = 900;
    uint32_t Starts{};
    uint32_t Prepares{};

    void SetCallbacks(Callbacks aCallbacks) noexcept override
    {
        Hooks = std::move(aCallbacks);
    }
    bool Prepare(uint32_t aHours, bool aSleeping, bool aDriver) noexcept override
    {
        if (!Available)
            return false;
        ++Prepares;
        Open = true;
        Sleeping = aSleeping;
        Driver = aDriver;
        Hours = aHours;
        RemainingSeconds = aHours * 3600;
        Steps = 0;
        return true;
    }
    bool IsReady() const noexcept override
    {
        return Open && Loaded;
    }
    void Begin() noexcept override
    {
        Running = true;
        ++Starts;
    }
    void Step(uint32_t aRemainingSeconds = UINT32_MAX) noexcept override
    {
        if (!Running || !RemainingSeconds)
            return;
        const auto cSeconds = Driver ? std::min(TickSeconds, RemainingSeconds) : RemainingSeconds - aRemainingSeconds;
        RemainingSeconds -= cSeconds;
        if (Driver)
        {
            auto calendar = Engine.Calendar;
            calendar.TimeScale = 1.f;
            DateTime clock(calendar);
            clock.Update(cSeconds * 1000);
            const float cScale = Engine.Calendar.TimeScale;
            Engine.Calendar = clock.m_timeModel;
            Engine.Calendar.TimeScale = cScale;
        }
        ++Steps;
        if (Driver && Hooks.Stepped)
            Hooks.Stepped(RemainingSeconds);
    }
    void End() noexcept override
    {
        const bool cOpen = std::exchange(Open, false);
        Running = false;
        if (cOpen)
            World::Get().GetDispatcher().trigger(MenuEvent{"Sleep/Wait Menu", false});
    }
    bool Confirm(uint32_t aHours, bool aSleeping = false)
    {
        return Hooks.Request && Hooks.Request(aHours, aSleeping);
    }
    bool Cancel()
    {
        return Hooks.Cancel && Hooks.Cancel();
    }
};
