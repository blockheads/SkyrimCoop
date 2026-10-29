#include <Papyrus/SystemQuests.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/OwnEvents.h>
#include <Papyrus/ScriptTags.h>

#include <Events/SystemQuestLearnedEvent.h>
#include <FormIds.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESQuest.h>
#include <Misc/BSScript.h>
#include <Misc/GameVM.h>
#include <RTTI.h>
#include <Structs/SystemQuestState.h>
#include <TimeManager.h>
#include <World.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

namespace SystemQuests
{
namespace
{
OwnEvents::SystemQuestList s_list;

// Each quest's registrations as its natives set them, by the event they raise. Its lock is a leaf.
std::mutex s_timerLock;
std::map<uint32_t, std::map<std::string, SystemQuestState::Timer, std::less<>>> s_timers;

BSScript::IVirtualMachine* Vm() noexcept
{
    return GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
}

uint64_t HandleOf(uint32_t aQuest) noexcept
{
    auto* pQuest = Cast<TESQuest>(TESForm::GetById(aQuest));
    BSScript::IVirtualMachine* pVm = Vm();
    return pQuest && pVm ? pVm->GetObjectHandlePolicy()->GetHandleForObject(static_cast<uint32_t>(pQuest->formType), pQuest) : 0;
}

// A script variable as it was read, under the VM's lock: no engine calls then.
struct RawVariable
{
    std::string Script;
    std::string Name;
    BSScript::Variable::Type Type;
    BSScript::Variable::Data Data;
    std::string String;
    uint32_t Form = 0; // an object's: the form its handle names
};

// Every variable of the scripts bound to a handle. They follow the object, its base script's first
// (Object::GetVariable).
struct ReadVariables final : BSScript::IForEachScriptObjectFunctor
{
    uint32_t Visit(BSScript::Object* apObject, bool) override
    {
        if (!apObject || !apObject->pType)
            return 1;
        std::vector<const BSScript::ObjectTypeInfo*> chain;
        for (const BSScript::ObjectTypeInfo* pType = apObject->pType; pType; pType = pType->pParent)
            chain.push_back(pType);
        const char* cpScript = apObject->pType->name.AsAscii();
        auto* pValue = reinterpret_cast<BSScript::Variable*>(apObject + 1);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto* cpInfo = (*it)->data ? (*it)->GetVariables() : nullptr;
            for (uint32_t i = 0; i < (*it)->variableCount; ++i, ++pValue)
                if (cpInfo)
                    Add(cpScript ? cpScript : "", cpInfo[i].name.AsAscii(), *pValue);
        }
        return 1;
    }

    void Add(const char* acpScript, const char* acpName, const BSScript::Variable& acValue)
    {
        using Type = BSScript::Variable::Type;
        RawVariable raw{acpScript, acpName ? acpName : "", acValue.type, acValue.data};
        // A raw type past the basic ones is an object's class; an odd one an array of them (TypeInfo).
        const bool cObject = acValue.type >= Type::kMax && !(acValue.type & 1);
        if (acValue.type > Type::kBoolean && !cObject)
            return; // an array: not kept
        if (acValue.type == Type::kString)
            raw.String = acValue.data.s ? acValue.data.s : "";
        if (cObject)
        {
            const uint64_t cHandle = acValue.data.pObj ? acValue.data.pObj->GetHandle() : 0;
            if (!BSScript::IsFormHandle(cHandle))
                return; // an alias's or an effect's: not a form
            raw.Form = BSScript::FormIdOfHandle(cHandle);
        }
        Variables.push_back(std::move(raw));
    }

    std::vector<RawVariable> Variables;
};

std::vector<RawVariable> VariablesOf(uint32_t aQuest) noexcept
{
    ReadVariables reader;
    BSScript::IVirtualMachine* pVm = Vm();
    if (const uint64_t cHandle = HandleOf(aQuest); pVm && cHandle)
        pVm->ForEachBoundObject(cHandle, &reader);
    return std::move(reader.Variables);
}

// The globals each learned quest's scripts hold, once its scripts are bound (a quest from the save's list may not be
// yet: asked again, at most once a second).
std::atomic<int64_t> s_lastRead{0};

void ReadGlobals() noexcept
{
    const auto cQuests = s_list.WithoutGlobals();
    if (cQuests.empty())
        return;
    const int64_t cNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t last = s_lastRead.load(std::memory_order_relaxed);
    if (cNow - last < 1000 || !s_lastRead.compare_exchange_strong(last, cNow))
        return;
    for (const uint32_t cQuest : cQuests)
    {
        const auto cVariables = VariablesOf(cQuest);
        if (cVariables.empty())
            continue; // not bound yet: asked again later
        std::vector<uint32_t> globals;
        for (const RawVariable& cVariable : cVariables)
            if (cVariable.Form && !TimeData::IsCalendarGlobal(cVariable.Form) && Cast<TESGlobal>(TESForm::GetById(cVariable.Form)))
                globals.push_back(cVariable.Form);
        s_list.SetGlobals(cQuest, globals);
        spdlog::info("SystemQuests: quest {:X}'s scripts hold {} global(s) of each player's own", cQuest, globals.size());
    }
}

ScriptValue ToValue(const RawVariable& acRaw) noexcept
{
    using Type = BSScript::Variable::Type;
    switch (acRaw.Type)
    {
    case Type::kEmpty: return std::monostate{};
    case Type::kInteger: return acRaw.Data.i;
    case Type::kFloat: return acRaw.Data.f;
    case Type::kBoolean: return acRaw.Data.b;
    case Type::kString: return acRaw.String;
    default: break;
    }
    // A form from the game's data; a created one (another game cannot have it) is None.
    const auto cId = acRaw.Form ? ToSession(acRaw.Form) : std::nullopt;
    return cId ? ScriptValue{ScriptForm{0, *cId}} : ScriptValue{std::monostate{}};
}

LocalValue ToLocal(const ScriptValue& acValue) noexcept
{
    return MapForm<LocalForm>(acValue, [](const ScriptForm& acForm) -> std::optional<LocalForm> {
               return LocalForm{ToGame(acForm.Form)};
           }).value_or(std::monostate{});
}

// A native called on the quest's own script object (the method is found up its types: Form's timers).
LocalCall OnQuest(uint32_t aQuest, std::string aScript, std::string aNative, std::vector<LocalValue> aArguments = {}) noexcept
{
    return {.Class = std::move(aScript), .Function = std::move(aNative), .Self = LocalForm{aQuest}, .Arguments = std::move(aArguments)};
}
} // namespace

bool Learn(uint32_t aQuest, bool aTell) noexcept
{
    if (!aQuest || !s_list.Learn(aQuest))
        return false;
    spdlog::info("SystemQuests: quest {:X} is one per player from now on", aQuest);
    s_lastRead = 0;
    ReadGlobals();
    if (aTell)
        World::Get().GetRunner().Trigger(SystemQuestLearnedEvent{aQuest});
    return true;
}

bool Any() noexcept
{
    return s_list.Any();
}

bool Is(uint32_t aQuest) noexcept
{
    return s_list.Contains(aQuest);
}

bool IsGlobal(uint32_t aGlobal) noexcept
{
    ReadGlobals();
    return s_list.IsGlobal(aGlobal);
}

std::vector<uint32_t> All() noexcept
{
    return s_list.All();
}

void Clear() noexcept
{
    s_list.Clear();
    std::scoped_lock _{s_timerLock};
    s_timers.clear();
}

void TimerCalled(uint32_t aQuest, std::string_view aNative, float aInterval) noexcept
{
    const GuestScripts::TimerNative* cpNative = GuestScripts::FindTimerNative(aNative);
    if (!cpNative || !Is(aQuest))
        return;
    std::scoped_lock _{s_timerLock};
    auto& timers = s_timers[aQuest];
    const std::string cEvent(cpNative->Events[0]);
    if (cpNative->Kind == GuestScripts::Timer::kStop)
        timers.erase(cEvent);
    else
        timers[cEvent] = {std::string(aNative), aInterval};
}

std::string ReadState() noexcept
{
    ReadGlobals();
    SystemQuestState state;
    for (const uint32_t cQuest : s_list.All())
    {
        const auto cId = ToSession(cQuest);
        if (!cId)
            continue;
        SystemQuestState::Quest& quest = state.Quests.emplace_back(SystemQuestState::Quest{*cId});
        for (const RawVariable& cRaw : VariablesOf(cQuest))
            quest.Variables.push_back({cRaw.Script, cRaw.Name, ToValue(cRaw)});
        std::scoped_lock _{s_timerLock};
        if (const auto it = s_timers.find(cQuest); it != s_timers.end())
            for (const auto& [cEvent, cTimer] : it->second)
                quest.Timers.push_back(cTimer);
    }
    for (const uint32_t cGlobal : s_list.Globals())
        if (const auto* cpGlobal = Cast<TESGlobal>(TESForm::GetById(cGlobal)))
            if (const auto cId = ToSession(cGlobal))
                state.Globals.push_back({*cId, cpGlobal->f});
    return state.Encode();
}

void ApplyState(std::string_view aState) noexcept
{
    BSScript::IVirtualMachine* pVm = Vm();
    if (!pVm)
        return;
    const auto cState = SystemQuestState::Decode(aState);
    for (const uint32_t cQuest : s_list.All())
    {
        const auto cId = ToSession(cQuest);
        const SystemQuestState::Quest* cpKept = cState && cId ? cState->Find(*cId) : nullptr;
        const uint64_t cHandle = HandleOf(cQuest);
        if (!cHandle)
            continue;
        const auto cVariables = VariablesOf(cQuest);
        const std::string cScript = cVariables.empty() ? std::string("Quest") : cVariables.front().Script;
        {
            std::scoped_lock _{s_timerLock};
            s_timers.erase(cQuest);
        }
        if (!cpKept)
        {
            // The save's instance is the host's player's: this player's starts over, as the quest's reset does.
            pVm->ResetAllBoundObjects(cHandle);
            spdlog::info("SystemQuests: quest {:X} has no state of this player's: its scripts start over", cQuest);
        }
        else
        {
            for (const SystemQuestState::Variable& cVariable : cpKept->Variables)
            {
                BSScript::Object* pObject = nullptr;
                if (!pVm->FindBoundObject(cHandle, cVariable.Script.c_str(), &pObject) || !pObject)
                    continue;
                if (BSScript::Variable* pVariable = pObject->GetVariable(cVariable.Name.c_str()))
                    GlobalCall::Write(*pVariable, ToLocal(cVariable.Value));
                pObject->DecRef();
            }
            spdlog::info("SystemQuests: quest {:X} is this player's again: {} variable(s), {} registration(s)", cQuest, cpKept->Variables.size(),
                         cpKept->Timers.size());
        }
        // The save's registrations go (the host's player's), then this player's are made again.
        std::vector<SystemQuestState::Timer> timers = cpKept ? cpKept->Timers : std::vector<SystemQuestState::Timer>{};
        GlobalCall::QueueCall(OnQuest(cQuest, cScript, "UnregisterForUpdate"));
        GlobalCall::QueueCall(OnQuest(cQuest, cScript, "UnregisterForUpdateGameTime"), [cQuest, cScript, timers = std::move(timers)](LocalValue) {
            for (const SystemQuestState::Timer& cTimer : timers)
                GlobalCall::QueueCall(OnQuest(cQuest, cScript, cTimer.Native, {cTimer.Interval}));
        });
    }
    if (!cState)
        return;
    for (const SystemQuestState::Global& cGlobal : cState->Globals)
        if (auto* pGlobal = Cast<TESGlobal>(TESForm::GetById(ToGame(cGlobal.Id))); pGlobal && IsGlobal(pGlobal->formID))
            pGlobal->f = cGlobal.Value;
}
} // namespace SystemQuests
