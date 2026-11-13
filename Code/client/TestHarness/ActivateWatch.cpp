// Who wakes a reference's bodies (docs/re/systems/physics.md, "Woken still"): calls of hkpEntity::Activate (60849) on
// its bodies and of markIslandActive (61525) on their islands, each with its call stack, and each awake change.
#include "ActivateWatch.h"
#include "Commands.h"

#include <Havok/bhkCollision.h>
#include <MinHook.h>
#include <NetImmerse/NiNode.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace TestHarness::ActivateWatch
{
namespace
{
// Both void, as read in IDA (60849: CommonLib-NG's hkpEntity::Activate; 61525: Havok's markIslandActive).
using TActivate = void(void*);
using TMarkIsland = void(void*, void*);
TActivate* RealActivate{};
TMarkIsland* RealMarkIsland{};

constexpr size_t cMaxEvents = 4096; // a watch nobody reads stops growing here
constexpr USHORT cStackDepth = 48;

struct Event
{
    int64_t Time{};
    const char* What{};
    uint32_t Reference{};
    uint32_t Thread{};
    std::array<void*, cStackDepth> Stack{};
    USHORT Depth{};
    // markIslandActive: the activated body's reference, its base and motion type
    uint32_t Through{};
    uint32_t ThroughBase{};
    int ThroughMotion{-1};
};

// Held only to read the maps or push an event, never around an engine call: the hooks run under the world's write
// lock, and Poll's body reads take its read lock.
std::mutex s_mutex;
std::atomic<bool> s_watching{};
std::set<uint32_t> s_references;
std::unordered_map<const void*, uint32_t> s_bodies;  // hkpRigidBody -> its reference
std::unordered_map<const void*, uint32_t> s_islands; // its simulation island, as of the last frame
std::map<const void*, bool> s_awake;
std::vector<Event> s_events;
size_t s_dropped{};

// The body hkpEntity::Activate was called on, while it runs: what markIslandActive's island was woken through.
thread_local const hkpRigidBody* t_activating{};

int64_t Now() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

Event Capture(const char* acpWhat, uint32_t aReference) noexcept
{
    Event event{.Time = Now(), .What = acpWhat, .Reference = aReference, .Thread = GetCurrentThreadId()};
    event.Depth = RtlCaptureStackBackTrace(2, cStackDepth, event.Stack.data(), nullptr);
    return event;
}

void PushLocked(Event&& aEvent) noexcept
{
    if (s_events.size() < cMaxEvents)
        s_events.push_back(std::move(aEvent));
    else
        ++s_dropped;
}

std::optional<uint32_t> Find(const std::unordered_map<const void*, uint32_t>& acMap, const void* apKey)
{
    std::scoped_lock _{s_mutex};
    const auto it = acMap.find(apKey);
    return it == acMap.end() ? std::nullopt : std::optional(it->second);
}

void HookActivate(void* apEntity)
{
    if (s_watching.load(std::memory_order_relaxed))
        if (const auto cReference = Find(s_bodies, apEntity))
        {
            Event event = Capture("activate", *cReference);
            std::scoped_lock _{s_mutex};
            PushLocked(std::move(event));
        }
    const auto* pPrevious = std::exchange(t_activating, static_cast<const hkpRigidBody*>(apEntity));
    RealActivate(apEntity);
    t_activating = pPrevious;
}

void HookMarkIsland(void* apWorld, void* apIsland)
{
    if (s_watching.load(std::memory_order_relaxed))
        if (const auto cReference = Find(s_islands, apIsland))
        {
            Event event = Capture("mark_island", *cReference);
            // Through which body, and whose: ReferenceOf reads its collidable's owner.
            if (const hkpRigidBody* pBody = t_activating)
            {
                const TESObjectREFR* pOwner = ReferenceOf(*pBody);
                event.Through = pOwner ? pOwner->formID : 0;
                event.ThroughBase = pOwner && pOwner->baseForm ? pOwner->baseForm->formID : 0;
                event.ThroughMotion = pBody->motionType;
            }
            std::scoped_lock _{s_mutex};
            PushLocked(std::move(event));
        }
    RealMarkIsland(apWorld, apIsland);
}

bool Install()
{
    static bool s_installed = false;
    if (s_installed)
        return true;
    VersionDbPtr<void> activate(60849);   // hkpEntity::Activate
    VersionDbPtr<void> markIsland(61525); // hkpWorldOperationUtil::markIslandActive (our name)
    if (MH_CreateHook(activate.GetPtr(), reinterpret_cast<void*>(&HookActivate), reinterpret_cast<void**>(&RealActivate)) != MH_OK ||
        MH_CreateHook(markIsland.GetPtr(), reinterpret_cast<void*>(&HookMarkIsland), reinterpret_cast<void**>(&RealMarkIsland)) != MH_OK ||
        MH_QueueEnableHook(activate.GetPtr()) != MH_OK || MH_QueueEnableHook(markIsland.GetPtr()) != MH_OK || MH_ApplyQueued() != MH_OK)
        return false;
    s_installed = true;
    return true;
}

// Each frame as "<module>+0x<offset>", named on the game thread (not in the hooks: the loader lock).
nlohmann::json StackJson(const Event& acEvent)
{
    auto stack = nlohmann::json::array();
    for (USHORT i = 0; i < acEvent.Depth; ++i)
    {
        HMODULE module{};
        char name[MAX_PATH]{};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(acEvent.Stack[i]), &module) &&
            GetModuleFileNameA(module, name, MAX_PATH))
        {
            std::string file = name;
            file = file.substr(file.find_last_of("\\/") + 1);
            stack.push_back(std::format("{}+0x{:X}", file, reinterpret_cast<uintptr_t>(acEvent.Stack[i]) - reinterpret_cast<uintptr_t>(module)));
        }
        else
            stack.push_back(std::format("0x{:X}", reinterpret_cast<uintptr_t>(acEvent.Stack[i])));
    }
    return stack;
}

nlohmann::json EventJson(const Event& acEvent)
{
    nlohmann::json event = {{"t", acEvent.Time}, {"what", acEvent.What}, {"ref", acEvent.Reference}};
    if (acEvent.Depth)
        event.update({{"thread", acEvent.Thread}, {"stack", StackJson(acEvent)}});
    if (acEvent.ThroughMotion >= 0)
        event["through"] = {{"ref", acEvent.Through}, {"motion", acEvent.ThroughMotion}, {"base", acEvent.ThroughBase}};
    return event;
}

// {"cmd": "activate_watch", "refs": [id, ...]}: starts watching (adds them); {} takes the events; {"stop": true} the rest.
nlohmann::json Command(Harness&, const nlohmann::json& acRequest)
{
    if (acRequest.contains("refs"))
    {
        if (!Install())
            return {{"ok", false}, {"error", "could not hook 60849 / 61525"}};
        std::scoped_lock _{s_mutex};
        for (const uint32_t cId : acRequest["refs"])
            s_references.insert(cId);
        s_watching = true;
    }
    std::vector<Event> taken;
    size_t dropped{};
    {
        std::scoped_lock _{s_mutex};
        taken = std::exchange(s_events, {});
        dropped = std::exchange(s_dropped, 0);
        if (acRequest.value("stop", false))
        {
            s_watching = false;
            s_references.clear();
            s_bodies.clear();
            s_islands.clear();
            s_awake.clear();
        }
    }
    auto events = nlohmann::json::array();
    for (const Event& cEvent : taken)
        events.push_back(EventJson(cEvent));
    // The hook files this game left out (HookSwitch.h logs them before the log opens).
    char disabled[2048]{};
    GetEnvironmentVariableA("SKYRIMCOOP_DISABLE_HOOKS", disabled, sizeof(disabled));
    return {{"ok", true}, {"events", events}, {"dropped", dropped}, {"disabled_hooks", std::string(disabled)}};
}

constexpr TestHarness::Command cCommands[] = {{"activate_watch", true, &Command}};
} // namespace

bool Watching() noexcept
{
    return s_watching.load(std::memory_order_relaxed);
}

void Poll()
{
    std::set<uint32_t> references;
    std::map<const void*, bool> awake;
    {
        std::scoped_lock _{s_mutex};
        references = s_references;
        awake = s_awake;
    }
    // The engine reads (ReadBody takes the world's lock) with the recorder's lock released.
    std::unordered_map<const void*, uint32_t> bodies;
    std::unordered_map<const void*, uint32_t> islands;
    std::vector<Event> changes;
    for (const uint32_t cId : references)
    {
        auto* pReference = Cast<TESObjectREFR>(TESForm::GetById(cId));
        NiNode* p3D = pReference && !pReference->IsDeleted() ? pReference->GetNiNode() : nullptr;
        if (!p3D)
            continue;
        for (bhkWorldObject* pBody : RigidBodiesOf(*p3D))
        {
            if (!pBody || !pBody->referencedObject)
                continue;
            const hkpRigidBody* pRigid = pBody->referencedObject;
            bodies[pRigid] = cId;
            if (pRigid->simulationIsland)
                islands[pRigid->simulationIsland] = cId;
            BodyRead read;
            if (!ReadBody(*pBody, read))
                continue;
            auto [it, cAdded] = awake.try_emplace(pRigid, read.Active);
            if (!cAdded && it->second != read.Active)
                changes.push_back({.Time = Now(), .What = read.Active ? "woke" : "slept", .Reference = cId});
            it->second = read.Active;
        }
    }
    std::scoped_lock _{s_mutex};
    if (!s_watching)
        return;
    s_bodies = std::move(bodies);
    s_islands = std::move(islands);
    s_awake = std::move(awake);
    for (Event& change : changes)
        PushLocked(std::move(change));
}

std::span<const TestHarness::Command> Commands()
{
    return cCommands;
}
} // namespace TestHarness::ActivateWatch
