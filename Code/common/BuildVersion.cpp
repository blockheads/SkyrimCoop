#include "BuildVersion.h"

#include <BuildInfo.h>

const char* BuildBranch() noexcept
{
    return BUILD_BRANCH;
}

const char* BuildCommit() noexcept
{
    return BUILD_COMMIT;
}
