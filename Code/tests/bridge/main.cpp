#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <rpmalloc.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

__attribute__((constructor(101))) static void EarlyRpmallocInit()
{
    rpmalloc_initialize();
}

int main(int argc, char* argv[])
{
    spdlog::stdout_color_mt("BridgeTest"); // the server console's output
    spdlog::set_level(spdlog::level::warn);
    return Catch::Session().run(argc, argv);
}
