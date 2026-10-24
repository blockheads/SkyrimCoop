#include <MainLoopTasks.h>

#include <TaskQueue.h>

namespace MainLoopTasks
{
namespace
{
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

void Run() noexcept
{
    Tasks().Drain();
}
} // namespace MainLoopTasks
