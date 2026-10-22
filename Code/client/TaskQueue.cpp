#include <TaskQueue.h>

    void TaskQueue::Add(std::function<void()> aFunction) noexcept
    {
        std::scoped_lock _{ m_mutex };

        UnsafeAdd(std::move(aFunction));
    }

    bool TaskQueue::Pop(std::function<void()>& aResultFunction) noexcept
    {
        std::scoped_lock _{ m_mutex };

        return UnsafePop(aResultFunction);
    }

    size_t TaskQueue::Drain() noexcept
    {
        // Take the tasks out under the lock, but run them without it: a task may queue more work
        // (e.g. a game hook firing while the task runs), which would otherwise deadlock on m_mutex.
        decltype(m_tasks) tasks;
        {
            std::scoped_lock _{ m_mutex };
            std::swap(tasks, m_tasks);
        }

        size_t count = 0;
        while (!tasks.empty())
        {
            tasks.front()();
            tasks.pop();
            ++count;
        }

        return count;
    }

    void TaskQueue::UnsafeAdd(std::function<void()> aFunction) noexcept
    {
        m_tasks.emplace(std::move(aFunction));
    }

    bool TaskQueue::UnsafePop(std::function<void()>& aResultFunction) noexcept
    {
        if (m_tasks.empty())
            return false;

        aResultFunction = std::move(m_tasks.front());

        m_tasks.pop();

        return true;
    }

    std::mutex& TaskQueue::Lock() noexcept
    {
        return m_mutex;
    }
