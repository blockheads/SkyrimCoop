#pragma once

#include <Components/BaseFormComponent.h>

#include <cstddef>
#include <cstdint>

// CommonLibSSE-NG BGSDestructibleObjectForm: a base's destruction data (DEST), its stages by health percent.
struct BGSDestructibleObjectForm : BaseFormComponent
{
    struct Data
    {
        uint32_t health; // the full health (DEST)
        int8_t numStages;
    };

    Data* data;
};

static_assert(offsetof(BGSDestructibleObjectForm, data) == 0x8);
static_assert(sizeof(BGSDestructibleObjectForm) == 0x10);
