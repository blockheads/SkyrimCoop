#pragma once

#include "TESForm.h"

struct BGSListForm : TESForm
{
    GameArray<TESForm*> ArrayOfForms{};
    GameArray<uint32_t>* pScriptAddedTempFormA{};
    uint32_t iScriptAddedFormCount{};
};

SKYRIM_STRUCT_ASSERT(sizeof(BGSListForm) == 0x48);
