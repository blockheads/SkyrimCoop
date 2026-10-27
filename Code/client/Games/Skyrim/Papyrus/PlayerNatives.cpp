#include <Papyrus/PlayerNatives.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/NativeReach.h>

#include <Events/RoutedCallEvent.h>
#include <Forms/TESQuest.h>
#include <Papyrus/GuestScripts.h>
#include <Papyrus/OwnEvents.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/BodyScripts.h>
#include <Games/EngineNames.h>
#include <HookSwitch.h>
#include <Misc/GameVM.h>
#include <World.h>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace PlayerNatives
{
namespace
{
// The stacks waiting on a guest's game (stack id -> what the native returns). Its lock is a leaf.
std::mutex s_waitingLock;
std::map<uint32_t, BSScript::Variable::Type> s_waiting;
std::atomic<bool> s_anyWaited{false}; // until then a stack ending skips the lock
std::atomic<bool> s_isGuest{false};

// The quests this guest's own scripts started.
OwnEvents::OwnQuests s_ownQuests;

// TESQuest::EnsureQuestStarted (25003, CommonLibSSE-NG `EnsureQuestStarted(bool& a_result, bool a_startNow)`): every
// start, Quest.Start's and the engine's.
using TQuestStarted = bool(TESQuest*, bool*, bool);
TQuestStarted* RealQuestStarted = nullptr;

bool HookQuestStarted(TESQuest* apThis, bool* apResult, bool aNow)
{
    const bool cWasRunning = apThis && (apThis->flags & TESQuest::Flags::Enabled);
    const bool cOk = RealQuestStarted(apThis, apResult, aNow);
    if (apThis && s_ownQuests.Started(apThis->formID, cWasRunning, cOk, s_isGuest.load(std::memory_order_relaxed), GuestScripts::NativeGuest()))
        spdlog::info("PlayerNatives: quest {:X} started by this guest's own script: its own", apThis->formID);
    if (apThis && cOk && !cWasRunning)
        BodyScripts::QuestStarted(apThis->formID, GuestScripts::NativeScript());
    return cOk;
}

Hooks::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(25003, &RealQuestStarted, HookQuestStarted);
});


} // namespace

void SetGuest(bool aIsGuest) noexcept
{
    s_isGuest = aIsGuest;
    s_ownQuests.Clear();
}

bool IsGuest() noexcept
{
    return s_isGuest.load(std::memory_order_relaxed);
}

bool IsOwnQuest(uint32_t aFormId) noexcept
{
    return s_ownQuests.Contains(aFormId);
}

BSScript::CallResult SendToHost(BSScript::IFunction* apFunction, BSScript::Stack& aStack, LocalCall aCall) noexcept
{
    aStack.returnValue.Clear();
    if (apFunction->GetReturnType().type == BSScript::Variable::kBoolean)
        aStack.returnValue.Set(true);
    spdlog::info("PlayerNatives: {}.{} from this guest's script goes to the host", aCall.Class, aCall.Function);
    World::Get().GetRunner().Trigger(RoutedCallEvent{.To = CallTo::kHost, .Call = std::move(aCall)});
    return BSScript::CallResult::kCompleted;
}

BSScript::CallResult SendToGuest(BSScript::IFunction* apFunction, BSScript::Stack& aStack, BSScript::IVirtualMachine* apVm, uint32_t aGuest,
                                 LocalCall aCall) noexcept
{
    RoutedCallEvent event{.To = CallTo::kOwner, .GuestFormId = aGuest, .Call = std::move(aCall)};
    if (apFunction->GetReturnType().type == BSScript::Variable::kEmpty)
    {
        aStack.returnValue.Clear();
        World::Get().GetRunner().Trigger(std::move(event));
        return BSScript::CallResult::kCompleted;
    }
    // It waits as on a latent native (NativeFunctionBase::Call 104651: 104851, then kInProgress) until Answer.
    {
        std::scoped_lock _{s_waitingLock};
        s_waiting[aStack.stackId] = apFunction->GetReturnType().type;
        s_anyWaited = true;
    }
    reinterpret_cast<BSScript::VirtualMachine*>(apVm)->WaitOnLatent(aStack.stackId);
    event.ReplyId = aStack.stackId;
    World::Get().GetRunner().Trigger(std::move(event));
    return BSScript::CallResult::kInProgress;
}

void Answer(uint32_t aReplyId, const LocalValue& acResult) noexcept
{
    BSScript::Variable::Type returnType;
    {
        std::scoped_lock _{s_waitingLock};
        auto waiting = s_waiting.extract(aReplyId);
        if (!waiting)
            return; // the stack ended meanwhile
        returnType = waiting.mapped();
    }
    auto* pVm = GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
    if (!pVm)
        return;
    BSScript::Variable result;
    GlobalCall::Write(result, acResult);
    // None for a number, a bool or a string: it did not run there.
    switch (result.type == BSScript::Variable::kEmpty ? returnType : BSScript::Variable::kEmpty)
    {
    case BSScript::Variable::kInteger: result.Set(0); break;
    case BSScript::Variable::kFloat: result.Set(0.f); break;
    case BSScript::Variable::kBoolean: result.Set(false); break;
    case BSScript::Variable::kString: result.Set(""); break;
    default: break;
    }
    pVm->ReturnFromLatent(aReplyId, result);
}

void StackEnded(uint32_t aStackId) noexcept
{
    if (!s_anyWaited.load(std::memory_order_relaxed))
        return;
    std::scoped_lock _{s_waitingLock};
    s_waiting.erase(aStackId);
}
} // namespace PlayerNatives
