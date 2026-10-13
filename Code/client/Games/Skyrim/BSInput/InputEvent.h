#pragma once

#include <Misc/BSFixedString.h>

// The engine's input events, as BSTEventSource<InputEvent*>::SendEvent (68655) passes them as a chain
// (CommonLibSSE-NG InputEvent, IDEvent, ButtonEvent; docs/re/systems/input.md).
struct InputEvent
{
    enum Device : uint32_t
    {
        kKeyboard = 0,
        kMouse = 1,
        kGamepad = 2,
    };
    enum Type : uint32_t
    {
        kButton = 0,
        kMouseMove = 1,
        kChar = 2,
        kThumbstick = 3,
    };

    void* pVtable;
    Device device;
    Type eventType;
    InputEvent* pNext;
};
SKYRIM_STRUCT_ASSERT(sizeof(InputEvent) == 0x18);

// A key or button: idCode is the DirectInput scan code on the keyboard.
struct ButtonEvent
{
    InputEvent header;
    BSFixedString userEvent;
    uint32_t idCode;
    uint32_t pad24;
    float value;        // 1 while down, 0 on the release
    float heldDownSecs; // 0 on the press

    [[nodiscard]] bool IsPress() const noexcept { return value > 0.f && heldDownSecs == 0.f; }
    [[nodiscard]] bool IsRelease() const noexcept { return value == 0.f; }
};
SKYRIM_STRUCT_ASSERT(offsetof(ButtonEvent, userEvent) == 0x18);
SKYRIM_STRUCT_ASSERT(offsetof(ButtonEvent, idCode) == 0x20);
SKYRIM_STRUCT_ASSERT(offsetof(ButtonEvent, value) == 0x28);
SKYRIM_STRUCT_ASSERT(sizeof(ButtonEvent) == 0x30);

// The mouse moved by (mouseInputX, mouseInputY) since the last poll; idCode 0xA ("Cursor" in menus).
struct MouseMoveEvent
{
    InputEvent header;
    BSFixedString userEvent;
    uint32_t idCode;
    uint32_t pad24;
    int32_t mouseInputX;
    int32_t mouseInputY;
};
SKYRIM_STRUCT_ASSERT(offsetof(MouseMoveEvent, idCode) == 0x20);
SKYRIM_STRUCT_ASSERT(offsetof(MouseMoveEvent, mouseInputX) == 0x28);
SKYRIM_STRUCT_ASSERT(sizeof(MouseMoveEvent) == 0x30);
