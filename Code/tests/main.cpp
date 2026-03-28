#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <rpmalloc.h>

// rpmalloc must be initialized before any static constructors that allocate
// (e.g., Catch2 TEST_CASE registration calls operator new, which goes through
// rpmalloc via AllocatorSymbolOverride). Priority 101 is the earliest safe
// user priority and runs before default (65535) static constructors.
__attribute__((constructor(101)))
static void EarlyRpmallocInit()
{
    rpmalloc_initialize();
}

int main(int argc, char* argv[])
{
    // rpmalloc already initialized by EarlyRpmallocInit
    int result = Catch::Session().run(argc, argv);
    rpmalloc_finalize();
    return result;
}
