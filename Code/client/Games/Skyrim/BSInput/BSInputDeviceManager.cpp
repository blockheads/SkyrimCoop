#include <BSGraphics/BSGraphicsRenderer.h>
#include <HookSwitch.h>
#include <Games/Skyrim/BSInput/InputEvent.h>

#include <Ui/UiInput.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

struct BSInputDeviceManager;

void (*BSInputDeviceManager_PollInputDevices)(BSInputDeviceManager*, float) = nullptr;
// BSTEventSource<InputEvent*>::SendEvent (68655): the frame's input chain to every sink (MenuControls,
// PlayerControls, ...); its source is BSInputDeviceManager.
void (*InputEventSource_SendEvent)(void*, InputEvent* const*) = nullptr;

void Hook_BSInputDeviceManager_PollInputDevices(BSInputDeviceManager* inputDeviceMgr, float afDelta)
{
    if (!BSGraphics::GetMainWindow()->IsForeground())
        return;

    BSInputDeviceManager_PollInputDevices(inputDeviceMgr, afDelta);
}

// While the chat line is open it takes the keys, and the game gets no input.
void Hook_InputEventSource_SendEvent(void* apSource, InputEvent* const* appChain)
{
    if (appChain && UiInput::Take(*appChain))
        return;
    InputEventSource_SendEvent(apSource, appChain);
}

static Hooks::Initializer s_initInputDeviceManager(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID_IMMEDIATE(68617, &BSInputDeviceManager_PollInputDevices, &Hook_BSInputDeviceManager_PollInputDevices);
        HOOK_ID_IMMEDIATE(68655, &InputEventSource_SendEvent, &Hook_InputEventSource_SendEvent);
    });
