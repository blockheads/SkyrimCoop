#pragma once

#include <Forms/TESForm.h>
#include <Games/Primitives.h>
#include <Misc/BSString.h>

// CommonLibSSE-NG SCRIPT_REFERENCED_OBJECT and Script.
struct ScriptReference
{
    BSString editorId;
    TESForm* form;
    uint32_t variableId;
    uint32_t pad1C;
};
static_assert(offsetof(ScriptReference, form) == 0x10 && sizeof(ScriptReference) == 0x20);

struct Script
{
    uint8_t pad0[0x24];
    uint32_t referenceCount; // 24, SCRIPT_HEADER::refObjectCount
    uint32_t dataSize;       // 28, SCRIPT_HEADER::dataSize
    uint8_t pad2C[0xC];
    const char* text; // 38
    const uint8_t* data; // 40, compiled bytecode
    uint8_t pad48[0x18];
    GameList<ScriptReference> references; // 60
};
static_assert(offsetof(Script, referenceCount) == 0x24);
static_assert(offsetof(Script, dataSize) == 0x28);
static_assert(offsetof(Script, text) == 0x38);
static_assert(offsetof(Script, data) == 0x40);
static_assert(offsetof(Script, references) == 0x60);
