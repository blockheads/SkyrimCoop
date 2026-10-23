#include <Papyrus/PlayerNatives.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/NativeReach.h>

#include <Events/PlayerNativeEvent.h>
#include <Games/EngineNames.h>
#include <Misc/GameVM.h>
#include <World.h>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace PlayerNatives
{
namespace
{
// The stacks waiting on a guest's game (stack id -> what the native returns). Its lock is a leaf.
std::mutex s_waitingLock;
std::map<uint32_t, BSScript::Variable::Type> s_waiting;
std::atomic<bool> s_anyWaited{false}; // until then a stack ending skips the lock

// A global native acting on this game's player without naming them (NativeReach). Game.GetPlayer is rule 2's own
// (GuestScripts, HookGetPlayer).
bool IsPlayerNative(BSScript::IFunction* apFunction) noexcept
{
    const char* cpClass = apFunction->GetObjectTypeName().AsAscii();
    const char* cpName = apFunction->GetName().AsAscii();
    const bool cIsGetPlayer = cpClass && cpName && SameName(cpClass, "Game") && SameName(cpName, "GetPlayer");
    return apFunction->GetIsStatic() && !cIsGetPlayer && NativeReach::ReachesThisPlayer(apFunction);
}

// The native's arguments from its frame; false: one the session cannot carry (GlobalCall::Read).
bool ReadArguments(BSScript::IFunction* apFunction, BSScript::StackFrame& aFrame, std::vector<LocalValue>& aArguments) noexcept
{
    const uint32_t cPage = aFrame.GetPageForFrame();
    for (uint32_t i = 0; i < apFunction->GetParamCount(); ++i)
    {
        auto value = GlobalCall::Read(*aFrame.GetStackFrameVariable(i, cPage));
        if (!value)
            return false;
        aArguments.push_back(std::move(*value));
    }
    return true;
}
} // namespace

std::optional<BSScript::CallResult> SendToGuest(BSScript::IFunction* apFunction, BSScript::Stack& aStack, BSScript::IVirtualMachine* apVm,
                                                uint32_t aGuest) noexcept
{
    if (!aStack.pTop || !IsPlayerNative(apFunction))
        return std::nullopt;
    PlayerNativeEvent event{.GuestFormId = aGuest, .Class = apFunction->GetObjectTypeName().AsAscii(), .Function = apFunction->GetName().AsAscii()};
    if (!ReadArguments(apFunction, *aStack.pTop, event.Arguments))
    {
        spdlog::warn("PlayerNatives: {}.{} for guest {:X} has an argument the session cannot carry: run on the host", event.Class, event.Function, aGuest);
        return std::nullopt;
    }
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
