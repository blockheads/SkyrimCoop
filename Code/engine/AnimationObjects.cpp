#include <AnimationObjects.h>

#include <Forms/TESForm.h>
#include <Primitives.h>
#include <NetImmerse/NiAVObject.h>
#include <TESObjectREFR.h>

#include <atomic>

namespace
{
// One held prop (AnimationObjects 43571 writes it; stride 0x28).
struct Entry
{
    uint32_t handle;      // 00 its reference's
    uint32_t pad04;       // 04
    TESForm* object;      // 08 TESObjectANIO
    NiAVObject* attached; // 10 its model, once loaded and attached
    void* queued;         // 18 QueuedAnimationObject while loading
    bool visible;         // 20 drawn
    uint8_t pad21[7];     // 21
};
static_assert(sizeof(Entry) == 0x28);
static_assert(offsetof(Entry, object) == 0x8);
static_assert(offsetof(Entry, visible) == 0x20);

struct Singleton
{
    uint64_t pad00;              // 00 BSTSingletonSDM
    GameArray<Entry> entries;    // 08 inline entries (data 08, capacity 10, size 18)
    volatile uint32_t lock;      // 20 a 0/1 spin flag (43571 and 43572 take it)
};
static_assert(offsetof(Singleton, entries) == 0x8);
static_assert(offsetof(Singleton, lock) == 0x20);

constexpr uint8_t cAnimObjectType = 0x53; // ANIO, as 42888 checks

Singleton* Get() noexcept
{
    POINTER_SKYRIMSE(Singleton*, s_singleton, 400328);
    return *s_singleton.Get();
}
} // namespace

namespace AnimationObjects
{
std::vector<Held> Of(const TESObjectREFR& acRef) noexcept
{
    std::vector<Held> held;
    Singleton* pObjects = Get();
    const uint32_t cHandle = acRef.GetHandle().handle.iBits;
    if (!pObjects || !cHandle)
        return held;
    std::atomic_ref<uint32_t> lock(const_cast<uint32_t&>(pObjects->lock));
    for (uint32_t expected = 0; !lock.compare_exchange_weak(expected, 1, std::memory_order_acquire); expected = 0)
        ;
    for (const Entry& cEntry : pObjects->entries)
        if (cEntry.handle == cHandle && cEntry.object)
            held.push_back({cEntry.object->formID, IsDrawn(cEntry.visible, cEntry.attached != nullptr,
                                                           cEntry.attached ? cEntry.attached->flags : 0)});
    lock.store(0, std::memory_order_release);
    return held;
}

void Hold(TESObjectREFR& aRef, const Held& acHeld) noexcept
{
    using TLoadDraw = void(Singleton*, const uint32_t*, TESForm*);
    POINTER_SKYRIMSE(TLoadDraw, s_load, 43571);
    POINTER_SKYRIMSE(TLoadDraw, s_draw, 43572);
    Singleton* pObjects = Get();
    TESForm* pObject = TESForm::GetById(acHeld.AnimObject);
    const uint32_t cHandle = aRef.GetHandle().handle.iBits;
    if (!pObjects || !pObject || static_cast<uint8_t>(pObject->formType) != cAnimObjectType || !cHandle)
        return;
    s_load.Get()(pObjects, &cHandle, pObject);
    if (acHeld.Drawn)
        s_draw.Get()(pObjects, &cHandle, pObject);
}
} // namespace AnimationObjects
