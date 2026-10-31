#include <catch2/catch.hpp>

#include <MainLoopTasks.h>

#include <thread>

TEST_CASE("A synchronous save requested during gameplay waits for the next frame", "[save-dispatch]")
{
    int saved = 0;
    const bool accepted = MainLoopTasks::RunOrQueue([&] {
        ++saved;
        return false;
    });
    CHECK(accepted);
    CHECK(saved == 0);
    MainLoopTasks::Run();
    CHECK(saved == 1);
    MainLoopTasks::Run();
    CHECK(saved == 1);
}

TEST_CASE("A save between frames completes before its caller reads the files", "[save-dispatch]")
{
    bool called = false;
    MainLoopTasks::Queue([&] {
        const bool saved = MainLoopTasks::RunOrQueue([&] {
            called = true;
            return false;
        });
        CHECK_FALSE(saved);
        CHECK(called);
    });
    MainLoopTasks::Run();
    CHECK(called);
}

TEST_CASE("A worker cannot borrow the main thread's safe save phase", "[save-dispatch]")
{
    const auto main = std::this_thread::get_id();
    std::thread::id savedOn;
    bool accepted = false;
    int saved = 0;
    MainLoopTasks::Queue([&] {
        std::thread worker([&] {
            accepted = MainLoopTasks::RunOrQueue([&] {
                savedOn = std::this_thread::get_id();
                ++saved;
                return true;
            });
        });
        worker.join();
        CHECK(accepted);
        CHECK(saved == 0);
    });
    MainLoopTasks::Run();
    CHECK(saved == 0);
    MainLoopTasks::Run();
    CHECK(saved == 1);
    CHECK(savedOn == main);
}

TEST_CASE("A nested drain keeps the caller in the safe save phase", "[save-dispatch]")
{
    bool saved = false;
    MainLoopTasks::Queue([&] {
        MainLoopTasks::Run();
        CHECK(MainLoopTasks::RunOrQueue([&] {
            saved = true;
            return true;
        }));
        CHECK(saved);
    });
    MainLoopTasks::Run();
    CHECK(saved);
}

TEST_CASE("Pending engine changes are cancelled when their session service goes", "[engine-scheduling]")
{
    int changed = 0;
    {
        MainLoopTasks::Scope session;
        session.Queue([&] { ++changed; });
    }
    MainLoopTasks::Run();
    CHECK(changed == 0);
    MainLoopTasks::Scope nextSession;
    nextSession.Queue([&] { changed += 10; });
    CHECK(changed == 0);
    MainLoopTasks::Run();
    CHECK(changed == 10);
}
