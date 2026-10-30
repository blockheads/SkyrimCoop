#include <MainLoopTasks.h>

#include <TaskQueue.h>

#include <utility>

namespace MainLoopTasks
{
namespace
{
thread_local bool t_running = false;

TaskQueue& Tasks() noexcept
{
    static TaskQueue s_tasks;
    return s_tasks;
}
} // namespace

void Queue(std::function<void()> aTask) noexcept
{
    Tasks().Add(std::move(aTask));
}

bool RunOrQueue(std::function<bool()> aTask) noexcept
{
    if (t_running)
        return aTask();
    Queue([task = std::move(aTask)] { task(); });
    return true;
}

void Run() noexcept
{
    const bool cWasRunning = std::exchange(t_running, true);
    Tasks().Drain();
    t_running = cWasRunning;
}
} // namespace MainLoopTasks
