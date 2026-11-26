#pragma once

#include <Structs/ActionEvent.h>
#include <cstdint>
#include <mutex>

// Animation jobs write these buffers while the VM and save/spawn readers take snapshots.
class AnimationActionState
{
  public:
    struct Attempt
    {
        ActionEvent Previous;
        uint64_t Revision = 0;
    };

    [[nodiscard]] ActionEvent Read() const noexcept
    {
        std::scoped_lock _{m_lock};
        return m_action;
    }

    void Write(const ActionEvent& acAction) noexcept
    {
        std::scoped_lock _{m_lock};
        m_action = acAction;
        ++m_revision;
    }

    [[nodiscard]] Attempt BeginAttempt() noexcept
    {
        std::scoped_lock _{m_lock};
        const Attempt cAttempt{m_action, ++m_revision};
        m_action.ActionId = 0;
        return cAttempt;
    }

    [[nodiscard]] bool FinishAttempt(uint32_t aActionId, const Attempt& acAttempt) noexcept
    {
        std::scoped_lock _{m_lock};
        if (m_action.ActionId == aActionId)
            return true;
        if (m_revision == acAttempt.Revision)
        {
            m_action = acAttempt.Previous;
            ++m_revision;
        }
        return false;
    }

  private:
    mutable std::mutex m_lock;
    ActionEvent m_action;
    uint64_t m_revision = 0;
};
