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

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <unordered_map>

namespace TestHarness::ActivateWatch
{
namespace
{
using TActivate = char(void*);
using TMarkIsland = int64_t(void*, void*);
TActivate* RealActivate{};
TMarkIsland* RealMarkIsland{};

std::atomic<bool> s_watching{};
std::mutex s_mutex;
std::set<uint32_t> s_references;
std::unordered_map<const void*, uint32_t> s_bodies;  // hkpRigidBody -> its reference
std::unordered_map<const void*, uint32_t> s_islands; // its simulation island, as of the last frame
std::map<const void*, bool> s_awake;
nlohmann::json s_events = nlohmann::json::array();

int64_t Now() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Each frame as "<module>+0x<offset>", the game's own as "SkyrimSE+".
nlohmann::json Stack()
{
    void* frames[48]{};
    const USHORT cCount = RtlCaptureStackBackTrace(2, 48, frames, nullptr);
    auto stack = nlohmann::json::array();
    for (USHORT i = 0; i < cCount; ++i)
    {
        HMODULE module{};
        char name[MAX_PATH]{};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module) &&
            GetModuleFileNameA(module, name, MAX_PATH))
        {
            std::string file = name;
            file = file.substr(file.find_last_of("\\/") + 1);
            stack.push_back(std::format("{}+0x{:X}", file,
                                        reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(module)));
        }
        else
            stack.push_back(std::format("0x{:X}", reinterpret_cast<uintptr_t>(frames[i])));
    }
    return stack;
}

// The body hkpEntity::Activate was called on, while it runs: what markIslandActive's island was woken through.
thread_local const hkpRigidBody* t_activating{};

nlohmann::json Record(const char* acpWhat, uint32_t aReference)
{
    s_events.push_back(
        {{"t", Now()}, {"what", acpWhat}, {"ref", aReference}, {"thread", GetCurrentThreadId()}, {"stack", Stack()}});
    return s_events.back();
}

char HookActivate(void* apEntity)
{
    if (s_watching.load(std::memory_order_relaxed))
    {
        std::scoped_lock _{s_mutex};
        if (const auto it = s_bodies.find(apEntity); it != s_bodies.end())
            Record("activate", it->second);
    }
    const auto* pPrevious = std::exchange(t_activating, static_cast<const hkpRigidBody*>(apEntity));
    const char cResult = RealActivate(apEntity);
    t_activating = pPrevious;
    return cResult;
}

int64_t HookMarkIsland(void* apWorld, void* apIsland)
{
    if (s_watching.load(std::memory_order_relaxed))
    {
        std::scoped_lock _{s_mutex};
        if (const auto it = s_islands.find(apIsland); it != s_islands.end())
        {
            Record("mark_island", it->second);
            // Through which body, and whose: a reference sharing the island (ReferenceOf reads its collidable's owner).
            if (const hkpRigidBody* pBody = t_activating)
            {
                const TESObjectREFR* pOwner = ReferenceOf(*pBody);
                s_events.back()["through"] = {{"ref", pOwner ? pOwner->formID : 0},
                                              {"motion", pBody->motionType},
                                              {"base", pOwner && pOwner->baseForm ? pOwner->baseForm->formID : 0}};
            }
        }
    }
    return RealMarkIsland(apWorld, apIsland);
}

bool Install()
{
    static bool s_installed = false;
    if (s_installed)
        return true;
    VersionDbPtr<void> activate(60849);   // hkpEntity::Activate
    VersionDbPtr<void> markIsland(61525); // hkpWorldOperationUtil::markIslandActive (our name)
    if (MH_CreateHook(activate.GetPtr(), reinterpret_cast<void*>(&HookActivate),
                      reinterpret_cast<void**>(&RealActivate)) != MH_OK ||
        MH_CreateHook(markIsland.GetPtr(), reinterpret_cast<void*>(&HookMarkIsland),
                      reinterpret_cast<void**>(&RealMarkIsland)) != MH_OK ||
        MH_QueueEnableHook(activate.GetPtr()) != MH_OK || MH_QueueEnableHook(markIsland.GetPtr()) != MH_OK ||
        MH_ApplyQueued() != MH_OK)
        return false;
    s_installed = true;
    return true;
}

// {"cmd": "activate_watch", "refs": [id, ...]}: starts watching (adds them); {} takes the events; {"stop": true} the
// rest.
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
    std::scoped_lock _{s_mutex};
    nlohmann::json events = std::exchange(s_events, nlohmann::json::array());
    if (acRequest.value("stop", false))
    {
        s_watching = false;
        s_references.clear();
        s_bodies.clear();
        s_islands.clear();
        s_awake.clear();
    }
    // The hook files this game left out (HookSwitch.h logs them before the log opens).
    char disabled[2048]{};
    GetEnvironmentVariableA("SKYRIMCOOP_DISABLE_HOOKS", disabled, sizeof(disabled));
    return {{"ok", true}, {"events", events}, {"disabled_hooks", std::string(disabled)}};
}

constexpr TestHarness::Command cCommands[] = {{"activate_watch", true, &Command}};
} // namespace

bool Watching() noexcept
{
    return s_watching.load(std::memory_order_relaxed);
}

void Poll()
{
    std::scoped_lock _{s_mutex};
    s_bodies.clear();
    s_islands.clear();
    for (const uint32_t cId : s_references)
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
            s_bodies[pRigid] = cId;
            if (pRigid->simulationIsland)
                s_islands[pRigid->simulationIsland] = cId;
            BodyRead read;
            if (!ReadBody(*pBody, read))
                continue;
            auto [it, cAdded] = s_awake.try_emplace(pRigid, read.Active);
            if (!cAdded && it->second != read.Active)
                s_events.push_back({{"t", Now()}, {"what", read.Active ? "woke" : "slept"}, {"ref", cId}});
            it->second = read.Active;
        }
    }
}

std::span<const TestHarness::Command> Commands()
{
    return cCommands;
}
} // namespace TestHarness::ActivateWatch
