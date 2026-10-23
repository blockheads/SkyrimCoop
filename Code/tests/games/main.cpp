#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <rpmalloc.h>
#include <spdlog/spdlog.h>

#include <cstdlib>

__attribute__((constructor(101))) static void EarlyRpmallocInit()
{
    rpmalloc_initialize();
}

int main(int argc, char* argv[])
{
    // Every leave logs a warning; GAMETESTS_LOG=debug (or info) shows the services' logs, for a failing flow.
    const char* cpLevel = std::getenv("GAMETESTS_LOG");
    spdlog::set_level(cpLevel ? spdlog::level::from_str(cpLevel) : spdlog::level::err);
    return Catch::Session().run(argc, argv);
}
