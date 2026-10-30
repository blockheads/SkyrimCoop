#pragma once

#include <functional>

// Engine work that must run on the main thread between frames (a save, a load, rebuilding a copy), never inside our update, which runs
// in the Papyrus VM's update job (HookVMUpdate): a save there waits on that job. Drained by HookMainLoop (36564).
namespace MainLoopTasks
{
void Queue(std::function<void()> aTask) noexcept;
// Outside the drain, true means queued; inside it, returns the task's actual result.
bool RunOrQueue(std::function<bool()> aTask) noexcept;
// HookMainLoop only.
void Run() noexcept;
} // namespace MainLoopTasks
