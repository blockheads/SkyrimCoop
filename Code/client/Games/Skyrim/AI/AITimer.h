#pragma once

#include <VersionDb.h>

struct AITimer
{
    static float GetAITime()
    {
        POINTER_SKYRIMSE(float, s_value, 404125);
        return *s_value;
    }

    float fTargetTime;
    float fStartTime;
};
