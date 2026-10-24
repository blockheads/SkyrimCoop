#pragma once

#include <cstddef>
#include <cstdint>

// A Scaleform value (CommonLibSSE-NG GFxValue): numbers, booleans and strings are read here.
struct GFxValue
{
    enum : uint32_t
    {
        kBoolean = 2,
        kNumber = 3,
        kString = 4,
        kManaged = 0x40,  // a managed string's value points at the string's pointer
        kTypeMask = 0x8F, // the type without its flags
    };

    void* objectInterface; // 00
    uint32_t type;         // 08
    uint32_t pad0C;        // 0C
    union
    {
        double number;
        bool boolean;
        const char* string;
        void* object;
    } value; // 10
};

// The arguments of a SWF's call into the game, a GameDelegate callback's (CommonLibSSE-NG FxDelegateArgs).
struct FxDelegateArgs
{
    GFxValue responseId;   // 00
    void* menu;            // 18, the FxDelegateHandler
    void* movie;           // 20, the GFxMovieView
    const GFxValue* args;  // 28
    uint32_t numArgs;      // 30
};

static_assert(sizeof(GFxValue) == 0x18);
static_assert(offsetof(GFxValue, value) == 0x10);
static_assert(offsetof(FxDelegateArgs, menu) == 0x18);
static_assert(offsetof(FxDelegateArgs, args) == 0x28);
static_assert(offsetof(FxDelegateArgs, numArgs) == 0x30);
