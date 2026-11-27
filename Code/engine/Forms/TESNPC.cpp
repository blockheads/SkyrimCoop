#include <Forms/TESNPC.h>
#include <VersionDb.h>

TESNPC* TESNPC::BuildLeveled(TESNPC* apOriginal, TESNPC* apPick) noexcept
{
    using TBuildLeveled = TESNPC*(TESNPC*, TESNPC*);
    POINTER_SKYRIMSE(TBuildLeveled, s_buildLeveled, 14375);
    return s_buildLeveled.Get()(apOriginal, apPick);
}
