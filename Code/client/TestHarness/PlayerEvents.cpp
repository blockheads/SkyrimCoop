#include <HookSwitch.h>
#include "Commands.h"

#include <Actor.h>
#include <Forms/TESForm.h>
#include <Misc/StoryEvents.h>
#include <MinHook.h>

#include <array>
#include <mutex>
#include <vector>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

// Spikes (docs/research/player-events.md) kept for measuring: the story manager's trace, and probes on the
// engine functions that read the player. The script-engine spikes became Games/Skyrim/Papyrus/GuestScripts.cpp.
namespace TestHarness::PlayerEvents
{
namespace
{
std::mutex s_lock;

// BGSStoryEventManager::AddEvent_Impl (32359): every story event, by its type index (QuestEvents.h).
using TAddEvent = uint32_t(void*, uint32_t, const void*);
TAddEvent* RealAddEvent = nullptr;

bool s_traceStory = false;
std::vector<std::array<uint32_t, 4>> s_capturedLocationChanges; // (old, new, event id, actor) raised here
std::vector<std::array<uint32_t, 3>> s_outcomes;                 // (event id, code, node or quest) processed here

uint32_t HookAddEvent(void* apThis, uint32_t aIndex, const void* apEvent)
{
    const uint32_t cId = RealAddEvent(apThis, aIndex, apEvent);
    std::scoped_lock _{s_lock};
    if (s_traceStory && apEvent && aIndex == StoryEvents::LocationChangeIndex())
    {
        auto* pEvent = static_cast<const StoryEvents::LocationChange*>(apEvent);
        const auto* pActor = TESObjectREFR::GetByHandle(pEvent->ActorHandle);
        s_capturedLocationChanges.push_back({pEvent->pOldLocation ? pEvent->pOldLocation->formID : 0,
                                             pEvent->pNewLocation ? pEvent->pNewLocation->formID : 0, cId, pActor ? pActor->formID : 0});
    }
    return cId;
}

Hooks::Initializer s_storyEventHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(32359, &RealAddEvent, HookAddEvent);
    });

// BGSStoryManager's own log of an event's processing (32374): what each node and quest did with it.
using TStoryLog = void(void*, int32_t, const uint32_t*, TESForm*, TESForm*, bool, float, float);
TStoryLog* RealStoryLog = nullptr;

void HookStoryLog(void* apThis, int32_t aCode, const uint32_t* apEvent, TESForm* apForm, TESForm* apNode, bool aAte, float aA, float aB)
{
    static constexpr const char* cCodes[] = {"started processing", "timed out", "finished", "node passed", "node failed conditions",
                                            "node at max quests", "quest waiting for reset", "quest already run", "quest started",
                                            "quest failed conditions", "quest failed to fill aliases", "quest timed out filling aliases"};
    if (s_traceStory)
    {
        spdlog::info("Story: event {:X} (type {}) {} {:X}", apEvent ? apEvent[0] : 0, apEvent ? apEvent[1] : 0,
                     aCode >= 0 && aCode < 12 ? cCodes[aCode] : "?", apForm && aCode >= 3 ? apForm->formID : 0);
        std::scoped_lock _{s_lock};
        if (apEvent && s_outcomes.size() < 4096)
            s_outcomes.push_back({apEvent[0], static_cast<uint32_t>(aCode), apForm && aCode >= 3 ? apForm->formID : 0});
    }
    RealStoryLog(apThis, aCode, apEvent, apForm, apNode, aAte, aA, aB);
}

Hooks::Initializer s_storyHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(32374, &RealStoryLog, HookStoryLog);
    });

// {"cmd": "story_trace", "on": true}: log the story manager's outcomes and capture the CLOC events raised here;
// returns those captured since the last call as (old location, new location, event id, actor), and each processed
// event's outcomes (32374's codes: 3 node passed, 8 quest started) as (event id, code, node or quest).
nlohmann::json StoryTrace(Harness&, const nlohmann::json& acRequest)
{
    std::scoped_lock _{s_lock};
    s_traceStory = acRequest.value("on", s_traceStory);
    auto captured = nlohmann::json::array();
    for (const auto& [cOld, cNew, cId, cActor] : s_capturedLocationChanges)
        captured.push_back({cOld, cNew, cId, cActor});
    s_capturedLocationChanges.clear();
    auto outcomes = nlohmann::json::array();
    for (const auto& [cId, cCode, cForm] : s_outcomes)
        outcomes.push_back({cId, cCode, cForm});
    s_outcomes.clear();
    return {{"ok", true}, {"captured", captured}, {"outcomes", outcomes}};
}

// {"cmd": "story_raise_location_change", "actor": id, "old": id, "new": id}: a location change raised for that actor
// (judged for it, as the host raises a guest's).
nlohmann::json StoryRaise(Harness&, const nlohmann::json& acRequest)
{
    auto* pActor = Cast<Actor>(TESForm::GetById(acRequest.value("actor", 0u)));
    if (!pActor)
        return Error("no such actor");
    const uint32_t cId = StoryEvents::RaiseLocationChange(*pActor, TESForm::GetById(acRequest.value("old", 0u)),
                                                         TESForm::GetById(acRequest.value("new", 0u)));
    return {{"ok", true}, {"event", cId}};
}

// Spike 3: which engine functions that read the player singleton (g_thePlayer, 403521) run, and with a
// remote player's copy among their first four arguments. Count-only stubs, installed on request.
struct ProbeCount
{
    uint64_t Calls;
    uint64_t CopyCalls;
};
constexpr size_t cMaxProbes = 2048;
ProbeCount s_probeCounts[cMaxProbes];
void* s_probeTrampolines[cMaxProbes];
uint32_t s_probeIds[cMaxProbes];
size_t s_probeCount = 0;
void* s_probeCopy = nullptr; // read by the stubs
uint8_t* s_probePage = nullptr;

// lock inc calls; if rcx, rdx, r8 or r9 is the copy, lock inc copy calls; jmp [trampoline]. Only rax and
// r11 change, both volatile and unused for arguments.
void* BuildCountStub(size_t aIndex)
{
    constexpr size_t cStubSize = 80;
    if (!s_probePage)
        s_probePage = static_cast<uint8_t*>(VirtualAlloc(nullptr, cMaxProbes * cStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!s_probePage)
        return nullptr;
    uint8_t* pStub = s_probePage + aIndex * cStubSize;
    size_t at = 0;
    auto emit = [&](std::initializer_list<uint8_t> aBytes) { for (const auto cByte : aBytes) pStub[at++] = cByte; };
    auto emit64 = [&](const void* apValue) { std::memcpy(pStub + at, &apValue, 8); at += 8; };
    emit({0x48, 0xB8}); emit64(&s_probeCounts[aIndex]);  // mov rax, counts
    emit({0xF0, 0x48, 0xFF, 0x00});                       // lock inc qword [rax]
    emit({0x49, 0xBB}); emit64(&s_probeCopy);             // mov r11, &copy
    emit({0x4D, 0x8B, 0x1B});                             // mov r11, [r11]
    emit({0x4D, 0x85, 0xDB, 0x74, 0x19});                 // test r11, r11; jz done
    emit({0x4C, 0x39, 0xD9, 0x74, 0x0F});                 // cmp rcx, r11; je hit
    emit({0x4C, 0x39, 0xDA, 0x74, 0x0A});                 // cmp rdx, r11; je hit
    emit({0x4D, 0x39, 0xD8, 0x74, 0x05});                 // cmp r8, r11; je hit
    emit({0x4D, 0x39, 0xD9, 0x75, 0x05});                 // cmp r9, r11; jne done
    emit({0xF0, 0x48, 0xFF, 0x40, 0x08});                 // hit: lock inc qword [rax+8]
    emit({0x48, 0xB8}); emit64(&s_probeTrampolines[aIndex]); // done: mov rax, &trampoline
    emit({0x48, 0x8B, 0x00, 0xFF, 0xE0});                 // mov rax, [rax]; jmp rax
    FlushInstructionCache(GetCurrentProcess(), pStub, at);
    return pStub;
}

// {"cmd": "player_probe", "ids": [...]}: installs the stubs (once). {"copy": id}: counts reset, the copy set.
nlohmann::json Probe(Harness&, const nlohmann::json& acRequest)
{
    nlohmann::json failed = nlohmann::json::array();
    if (acRequest.contains("ids") && s_probeCount == 0)
    {
        for (const uint32_t cId : acRequest["ids"])
        {
            if (s_probeCount == cMaxProbes)
                break;
            VersionDbPtr<void> function(cId);
            void* pStub = function.GetPtr() ? BuildCountStub(s_probeCount) : nullptr;
            MH_STATUS status = pStub ? MH_CreateHook(function.GetPtr(), pStub, &s_probeTrampolines[s_probeCount]) : MH_UNKNOWN;
            if (status == MH_OK)
                status = MH_QueueEnableHook(function.GetPtr());
            if (status != MH_OK)
            {
                failed.push_back({cId, static_cast<int>(status)});
                continue;
            }
            s_probeIds[s_probeCount++] = cId;
        }
        if (MH_ApplyQueued() != MH_OK)
            return Error("MH_ApplyQueued failed");
    }
    std::memset(s_probeCounts, 0, sizeof(s_probeCounts));
    s_probeCopy = acRequest.contains("copy") ? TESForm::GetById(acRequest["copy"].get<uint32_t>()) : nullptr;
    return {{"ok", true}, {"probes", s_probeCount}, {"failed", failed}, {"copy", s_probeCopy != nullptr}};
}

nlohmann::json ProbeRead(Harness&, const nlohmann::json&)
{
    auto counts = nlohmann::json::object();
    for (size_t i = 0; i < s_probeCount; ++i)
        if (s_probeCounts[i].Calls)
            counts[std::to_string(s_probeIds[i])] = {s_probeCounts[i].Calls, s_probeCounts[i].CopyCalls};
    return {{"ok", true}, {"counts", counts}};
}

constexpr Command cCommands[] = {
    {"player_probe", false, &Probe},
    {"player_probe_read", false, &ProbeRead},
    {"story_trace", false, &StoryTrace},
    {"story_raise_location_change", true, &StoryRaise},
};
} // namespace
} // namespace TestHarness::PlayerEvents

namespace TestHarness
{
std::span<const Command> PlayerEventsCommands()
{
    return PlayerEvents::cCommands;
}
} // namespace TestHarness
