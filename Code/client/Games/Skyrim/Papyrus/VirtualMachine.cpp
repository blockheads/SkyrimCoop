#include <Misc/BSScript.h>
#include <HookSwitch.h>
#include <Misc/NativeFunction.h>

#include <World.h>
#include <Events/PapyrusFunctionRegisterEvent.h>
#include <Games/Memory.h>

TP_THIS_FUNCTION(TRegisterPapyrusFunction, void, BSScript::IVirtualMachine, NativeFunction*);
TP_THIS_FUNCTION(TBindEverythingToScript, void, BSScript::IVirtualMachine*);
TP_THIS_FUNCTION(TSignaturesMatch, bool, BSScript::NativeFunction, BSScript::NativeFunction*);

TRegisterPapyrusFunction* RealRegisterPapyrusFunction = nullptr;
TBindEverythingToScript* RealBindEverythingToScript = nullptr;
TSignaturesMatch* RealSignaturesMatch = nullptr;

// SkyrimTogetherVerifyLaunchScript.DidLaunchSkyrimTogether: the plugin is loaded.
static bool DidLaunchSkyrimTogether()
{
    return true;
}

void TP_MAKE_THISCALL(HookRegisterPapyrusFunction, BSScript::IVirtualMachine, NativeFunction* apFunction)
{
    // Synchronous: natives register on the main thread during VM setup, and hooks such as
    // HookActivate look them up (PAPYRUS_FUNCTION) before the first World::Update would
    // drain a queued event, caching a null function pointer for good.
    World::Get().GetDispatcher().trigger(
        PapyrusFunctionRegisterEvent(apFunction->functionName.AsAscii(), apFunction->typeName.AsAscii(), apFunction->functionAddress));

    TiltedPhoques::ThisCall(RealRegisterPapyrusFunction, apThis, apFunction);
}

void TP_MAKE_THISCALL(HookBindEverythingToScript, BSScript::IVirtualMachine*)
{
    // The VM owns bound functions and frees them with the game heap (e.g. when the
    // script class is not installed), so they must come from the game heap too.
    // Plain `new` used the DLL's allocator and crashed BSSmallBlockAllocator at startup.
    (*apThis)->BindNativeMethod(Memory::New<BSScript::DidLaunchSkyrimTogetherFunc>("DidLaunchSkyrimTogether", "SkyrimTogetherVerifyLaunchScript", DidLaunchSkyrimTogether, BSScript::Variable::kBoolean));

    TiltedPhoques::ThisCall(RealBindEverythingToScript, apThis);
}

bool TP_MAKE_THISCALL(HookSignaturesMatch, BSScript::NativeFunction, BSScript::NativeFunction* apOther)
{
    /*
    if (!strcmp(apThis->GetName().AsAscii(), "IsRemotePlayer"))
        DebugBreak();
    */

    return TiltedPhoques::ThisCall(RealSignaturesMatch, apThis, apOther);
}

static TiltedPhoques::Initializer s_vmHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TRegisterPapyrusFunction, s_registerPapyrusFunction, 104788);
        POINTER_SKYRIMSE(TBindEverythingToScript, s_bindEverythingToScript, 55739);
        POINTER_SKYRIMSE(TSignaturesMatch, s_signaturesMatch, 104359);

        RealRegisterPapyrusFunction = s_registerPapyrusFunction.Get();
        RealBindEverythingToScript = s_bindEverythingToScript.Get();
        RealSignaturesMatch = s_signaturesMatch.Get();

        TP_HOOK(&RealRegisterPapyrusFunction, HookRegisterPapyrusFunction);
        TP_HOOK(&RealBindEverythingToScript, HookBindEverythingToScript);
        TP_HOOK(&RealSignaturesMatch, HookSignaturesMatch);
    });
