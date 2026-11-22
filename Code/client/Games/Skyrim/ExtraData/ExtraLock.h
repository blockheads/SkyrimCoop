#pragma once

#include "ExtraData.h"

struct Lock;

// A reference's own lock (CommonLibSSE-NG ExtraLock); a load door without one uses its linked door's (20223).
struct ExtraLock : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Lock;

    Lock* lock{};
};

static_assert(sizeof(ExtraLock) == 0x18);
