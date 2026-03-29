#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <rpmalloc.h>

__attribute__((constructor(101)))
static void EarlyRpmallocInit()
{
    rpmalloc_initialize();
}

int main(int argc, char* argv[])
{
    int result = Catch::Session().run(argc, argv);
    rpmalloc_finalize();
    return result;
}
