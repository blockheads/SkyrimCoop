#include <HookSwitch.h>
#include "Commands.h"

#include <Forms/TESForm.h>
#include <MinHook.h>

#include <array>
#include <mutex>
#include <vector>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

// Spikes (docs/research/player-events.md) kept for measuring: forwarding a story event, and probes on the
// engine functions that read the player. The script-engine spikes became Games/Skyrim/Papyrus/GuestScripts.cpp.
namespace TestHarness::PlayerEvents
{
namespace
{
std::mutex s_lock;

// BGSStoryEventManager::AddEvent_Impl (32359): every story event, by its type index (QuestEvents.h).
using TAddEvent = uint32_t(void*, uint32_t, const void*);
TAddEvent* RealAddEvent = nullptr;

// Spike 4: the location change story event (CLOC), as PlayerCharacter::SetParentCell builds it (32288).
struct LocationChangeEvent
{
    uint32_t ActorHandle;  // 00
    uint32_t Pad04;
    TESForm* pOldLocation; // 08
    TESForm* pNewLocation; // 10
};
static_assert(sizeof(LocationChangeEvent) == 0x18);
bool s_traceStory = false;
std::vector<std::array<uint32_t, 3>> s_capturedLocationChanges; // (old, new, event id) the player raised

uint32_t LocationChangeIndex()
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380069); // the CLOC event type's registered index
    return *s_index.Get();
}

uint32_t HookAddEvent(void* apThis, uint32_t aIndex, const void* apEvent)
{
    const uint32_t cId = RealAddEvent(apThis, aIndex, apEvent);
    std::scoped_lock _{s_lock};
    if (s_traceStory && apEvent && aIndex == LocationChangeIndex())
    {
        auto* pEvent = static_cast<const LocationChangeEvent*>(apEvent);
        s_capturedLocationChanges.push_back({pEvent->pOldLocation ? pEvent->pOldLocation->formID : 0,
                                             pEvent->pNewLocation ? pEvent->pNewLocation->formID : 0, cId});
    }
    return cId;
}

Hooks::Initializer s_storyEventHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TAddEvent, s_addEvent, 32359);
        RealAddEvent = s_addEvent.Get();
        HOOK(&RealAddEvent, HookAddEvent);
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
        spdlog::info("Story: event {:X} (type {}) {} {:X}", apEvent ? apEvent[0] : 0, apEvent ? apEvent[1] : 0,
                     aCode >= 0 && aCode < 12 ? cCodes[aCode] : "?", apForm && aCode >= 3 ? apForm->formID : 0);
    RealStoryLog(apThis, aCode, apEvent, apForm, apNode, aAte, aA, aB);
}

Hooks::Initializer s_storyHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TStoryLog, s_storyLog, 32374);
        RealStoryLog = s_storyLog.Get();
        HOOK(&RealStoryLog, HookStoryLog);
    });

// {"cmd": "story_trace", "on": true}: log the story manager's outcomes and capture the player's CLOC events;
// returns those captured since the last call as (old location, new location, event id).
nlohmann::json StoryTrace(Harness&, const nlohmann::json& acRequest)
{
    std::scoped_lock _{s_lock};
    s_traceStory = acRequest.value("on", s_traceStory);
    auto captured = nlohmann::json::array();
    for (const auto& [cOld, cNew, cId] : s_capturedLocationChanges)
        captured.push_back({cOld, cNew, cId});
    s_capturedLocationChanges.clear();
    return {{"ok", true}, {"captured", captured}};
}

// {"cmd": "story_raise_location_change", "actor": id, "old": id, "new": id}: raise a CLOC as the engine does.
nlohmann::json StoryRaise(Harness&, const nlohmann::json& acRequest)
{
    auto* pActor = TESForm::GetById(acRequest.value("actor", 0u));
    if (!pActor)
        return Error("no such actor");
    using TBuild = LocationChangeEvent*(LocationChangeEvent*, TESForm*, TESForm*, TESForm*);
    using TGetManager = void*();
    POINTER_SKYRIMSE(TBuild, s_build, 32288);
    POINTER_SKYRIMSE(TGetManager, s_getManager, 22790);
    LocationChangeEvent event{};
    s_build(&event, pActor, TESForm::GetById(acRequest.value("old", 0u)), TESForm::GetById(acRequest.value("new", 0u)));
    const uint32_t cId = RealAddEvent(s_getManager.Get()(), LocationChangeIndex(), &event);
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
