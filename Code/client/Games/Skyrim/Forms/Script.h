#pragma once

// A compiled console or result script (CommonLibSSE-NG Script): only its source text is read.
struct Script
{
    uint8_t pad0[0x38];
    const char* text; // 38
};
SKYRIM_STRUCT_ASSERT(offsetof(Script, text) == 0x38);
