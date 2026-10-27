#pragma once

#include <cstdint>

// A message box's answer: Run is called with the button pressed (IMessageBoxCallback, vtable slot 1; CommonLibSSE-NG).
struct IMessageBoxCallback
{
    virtual ~IMessageBoxCallback() = default;
    virtual void Run(uint8_t aButton) = 0;

    volatile uint32_t refCount; // 08
    uint32_t pad0C;
};
static_assert(sizeof(IMessageBoxCallback) == 0x10);

// Message.Show's: Run returns the button to the script waiting in stackId (SkyrimScript::ScriptMessageBoxCallback,
// vtable 217737, created by papyrus Message::Show 55255; Run is ReturnFromLatent(stackId, button)).
struct ScriptMessageBoxCallback : IMessageBoxCallback
{
    uint32_t stackId; // 10
    uint32_t pad14;
    void* pVm; // 18
};
static_assert(offsetof(ScriptMessageBoxCallback, stackId) == 0x10 && sizeof(ScriptMessageBoxCallback) == 0x20);

// A queued message box (CommonLibSSE-NG MessageBoxData).
struct MessageBoxData
{
    virtual ~MessageBoxData() = default;

    uint8_t unk08[0x40 - 0x08];
    IMessageBoxCallback* pCallback; // 40, BSTSmartPointer
    int32_t menuDepth;              // 48
    uint8_t buttonPressOffset;      // 4C
    uint8_t pad4D[3];
};
static_assert(offsetof(MessageBoxData, pCallback) == 0x40 && sizeof(MessageBoxData) == 0x50);

namespace MessageBoxes
{
// The queued box Message.Show opened for the script waiting in aStackId, answered with aResult as its button's press
// would (CommonLibSSE-NG SelectOption): out of the queue, the menu hidden when none is left, then its callback. False:
// no such box.
bool AnswerStack(uint32_t aStackId, uint8_t aResult) noexcept;
} // namespace MessageBoxes
