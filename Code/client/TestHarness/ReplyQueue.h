#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <string>

namespace TestHarness
{
// One socket client's replies in request order: most ready at once, a large one finished off the game thread (its
// JSON built and written there, docs/re/systems/harness-cost.md). Pure: ClientTests.
class ReplyQueue
{
public:
    using TFinish = std::function<std::string()>;

    ReplyQueue() = default;
    ReplyQueue(ReplyQueue&&) noexcept = default;
    ReplyQueue& operator=(ReplyQueue&&) noexcept = default;
    ~ReplyQueue() { Wait(); }

    // Finished on a worker.
    void Push(TFinish aFinish) { m_pending.push_back(std::async(std::launch::async, std::move(aFinish))); }
    // Ready now: sent this frame unless an earlier one still runs.
    void Push(std::string aReply)
    {
        std::promise<std::string> ready;
        ready.set_value(std::move(aReply));
        m_pending.push_back(ready.get_future());
    }

    // The finished replies at the front, a line each; one still running holds back those after it.
    void Drain(std::string& aOut)
    {
        while (!m_pending.empty() && m_pending.front().wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            aOut += m_pending.front().get();
            aOut += '\n';
            m_pending.pop_front();
        }
    }

    [[nodiscard]] bool Empty() const noexcept { return m_pending.empty(); }

    // Before the plugin unloads: the workers run its code.
    void Wait() noexcept
    {
        for (auto& pending : m_pending)
            if (pending.valid())
                pending.wait();
    }

private:
    std::deque<std::future<std::string>> m_pending;
};
} // namespace TestHarness
