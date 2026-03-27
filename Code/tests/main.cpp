#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <rpmalloc.h>

int main(int argc, char* argv[])
{
    rpmalloc_initialize();
    int result = Catch::Session().run(argc, argv);
    rpmalloc_finalize();
    return result;
}
