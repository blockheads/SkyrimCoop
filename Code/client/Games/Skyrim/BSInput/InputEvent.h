#pragma once

#include <Games/Primitives.h>
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
static_assert(sizeof(InputEvent) == 0x18);

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
static_assert(offsetof(ButtonEvent, userEvent) == 0x18);
static_assert(offsetof(ButtonEvent, idCode) == 0x20);
static_assert(offsetof(ButtonEvent, value) == 0x28);
static_assert(sizeof(ButtonEvent) == 0x30);

// A typed character for text fields (the name entry, the console), as the keyboard sends it after its key;
// the engine's constructor (68634) sets device 5 and no next.
struct CharEvent
{
    InputEvent header;
    uint32_t keyCode; // the character, UTF-16
    uint32_t pad1C;
};
static_assert(offsetof(CharEvent, keyCode) == 0x18);
static_assert(sizeof(CharEvent) == 0x20);

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
static_assert(offsetof(MouseMoveEvent, idCode) == 0x20);
static_assert(offsetof(MouseMoveEvent, mouseInputX) == 0x28);
static_assert(sizeof(MouseMoveEvent) == 0x30);

// BSTEventSource<InputEvent*> (BSInputDeviceManager 402776 at offset 0): SendEvent (68655) calls each sink's
// ProcessEvent, vtable slot 1 `(sink, InputEvent* const* chain, source)`, until one returns 1 (stop). IDA 2026-10-21.
struct InputEventSource
{
    GameArray<void*> sinks;              // 00
    GameArray<void*> pendingRegisters;   // 18
    GameArray<void*> pendingUnregisters; // 30
    uint64_t lock;                       // 48
    bool notifying;                      // 50
};
static_assert(offsetof(InputEventSource, pendingRegisters) == 0x18);
static_assert(offsetof(InputEventSource, lock) == 0x48);
static_assert(offsetof(InputEventSource, notifying) == 0x50);
