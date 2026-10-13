#include <BSGraphics/BSGraphicsRenderer.h>
#include <HookSwitch.h>
#include <Games/Skyrim/BSInput/InputEvent.h>

#ifdef HAS_RMLUI
#include <Ui/UiInput.h>
#endif

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
#ifdef HAS_RMLUI
    if (appChain && UiInput::Take(*appChain))
        return;
#endif
    InputEventSource_SendEvent(apSource, appChain);
}

static TiltedPhoques::Initializer s_initInputDeviceManager(
    []()
    {
        const HookGate cGate(__FILE__);
        const VersionDbPtr<void> pollInputDevices(68617);
        const VersionDbPtr<void> sendEvent(68655);

        BSInputDeviceManager_PollInputDevices = reinterpret_cast<decltype(BSInputDeviceManager_PollInputDevices)>(pollInputDevices.GetPtr());
        InputEventSource_SendEvent = reinterpret_cast<decltype(InputEventSource_SendEvent)>(sendEvent.GetPtr());

        TP_HOOK_IMMEDIATE(&BSInputDeviceManager_PollInputDevices, &Hook_BSInputDeviceManager_PollInputDevices);
        TP_HOOK_IMMEDIATE(&InputEventSource_SendEvent, &Hook_InputEventSource_SendEvent);
    });
