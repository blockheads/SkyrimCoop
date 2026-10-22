#pragma once

#include <Forms/TESForm.h>
#include <Misc/BSString.h>

struct TESGlobal : TESForm
{
    // Record flag: Papyrus refuses to set it (GlobalVariable.SetValue, 55923).
    static constexpr uint32_t kConstant = 0x40;

    bool IsConstant() const noexcept { return flags & kConstant; }

    BSString unk14;
    uint8_t unk1C;
    uint8_t pad[3];
    union
    {
        uint32_t i;
        float f;
    };
};

static_assert(offsetof(TESGlobal, f) == 0x34);
