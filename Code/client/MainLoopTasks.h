#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

// Engine work that must run on the main thread between frames (a save, a load, rebuilding a copy), never inside our update, which runs
// in the Papyrus VM's update job (HookVMUpdate): a save there waits on that job. Drained by HookMainLoop (36564).
namespace MainLoopTasks
{
void Queue(std::function<void()> aTask) noexcept;
// Outside the drain, true means queued; inside it, returns the task's actual result.
bool RunOrQueue(std::function<bool()> aTask) noexcept;
// A service's queued tasks: dropped once the service is gone (it left the session).
struct Scope
{
    Scope() = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { m_alive->store(false); }

    void Queue(std::function<void()> aTask) const noexcept
    {
        MainLoopTasks::Queue([alive = m_alive, task = std::move(aTask)] {
            if (alive->load())
                task();
        });
    }

  private:
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
};
// HookMainLoop only.
void Run() noexcept;
} // namespace MainLoopTasks
