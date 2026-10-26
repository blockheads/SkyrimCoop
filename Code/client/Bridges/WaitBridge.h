#pragma once

#include <Bridges/IWaitBridge.h>

class WaitBridge final : public IWaitBridge
{
  public:
    ~WaitBridge() override;
    void SetCallbacks(Callbacks aCallbacks) noexcept override;
    bool Prepare(uint32_t aHours, bool aSleeping, bool aDriver) noexcept override;
    bool IsReady() const noexcept override;
    void Begin() noexcept override;
    void Step(uint32_t aRemainingSeconds) noexcept override;
    void End() noexcept override;
};
