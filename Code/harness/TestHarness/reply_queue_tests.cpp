#include <catch2/catch.hpp>

#include <TestHarness/JsonReply.h>
#include <TestHarness/ReplyQueue.h>

#include <atomic>
#include <thread>

using TestHarness::ReplyQueue;

TEST_CASE("Replies finished off the game thread go out in request order", "[harness]")
{
    ReplyQueue queue;
    std::atomic<bool> release{false};
    // The first reply is slow (a whole world's JSON), the second quick.
    queue.Push([&release] {
        while (!release)
            std::this_thread::yield();
        return std::string("first");
    });
    queue.Push(std::string("second"));

    std::string out;
    queue.Drain(out);
    CHECK(out.empty()); // the ready one waits behind the slow one
    CHECK_FALSE(queue.Empty());

    release = true;
    queue.Wait();
    queue.Drain(out);
    CHECK(out == "first\nsecond\n");
    CHECK(queue.Empty());
}

TEST_CASE("A reply's work does not run on the thread that pushed it", "[harness]")
{
    ReplyQueue queue;
    const auto cCaller = std::this_thread::get_id();
    std::thread::id worker{};
    queue.Push([&worker] {
        worker = std::this_thread::get_id();
        return std::string("{}");
    });
    queue.Wait();
    std::string out;
    queue.Drain(out);
    CHECK(out == "{}\n");
    CHECK(worker != cCaller);
}

TEST_CASE("A ready reply goes out the frame its command ran", "[harness]")
{
    // A console_result takes the line's output: held for a frame, a load frame next would outlast the caller's wait.
    ReplyQueue queue;
    queue.Push(std::string("{\"ran\":true}"));
    std::string out;
    queue.Drain(out);
    CHECK(out == "{\"ran\":true}\n");
    CHECK(queue.Empty());
}

TEST_CASE("A closed client's unfinished reply is seen without waiting for it", "[harness]")
{
    // CommandServer keeps such a queue until Finished(): dropping it would wait on the game thread.
    ReplyQueue queue;
    std::atomic<bool> release{false};
    queue.Push([&release] {
        while (!release)
            std::this_thread::yield();
        return std::string("late");
    });
    queue.Push(std::string("ready"));
    CHECK_FALSE(queue.Finished());
    release = true;
    queue.Wait();
    CHECK(queue.Finished());
}

TEST_CASE("A reply whose JSON cannot be written answers as a refusal, the next one still goes", "[harness]")
{
    ReplyQueue queue;
    // Not UTF-8 (a model's node name): nlohmann's dump throws type_error 316.
    queue.Push([] { return TestHarness::DumpReply([] { return nlohmann::json{{"name", std::string("\xff\xfe")}}; }); });
    queue.Push([] { return TestHarness::DumpReply([] { return nlohmann::json{{"ok", true}}; }); });
    queue.Wait();
    std::string out;
    queue.Drain(out);
    const auto cFirst = nlohmann::json::parse(out.substr(0, out.find('\n')));
    CHECK(cFirst["ok"] == false);
    CHECK(cFirst["error"].get<std::string>().find("316") != std::string::npos);
    CHECK(out.substr(out.find('\n') + 1) == "{\"ok\":true}\n");
}
