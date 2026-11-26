#include <Interface/ItemMenuWatch.h>

#include <ItemMenuGate.h>
#include <ItemMenuRebuild.h>
#include <Interface/Menus/ItemMenus.h>
#include <Interface/UI.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace ItemMenuWatch
{
namespace
{
constexpr std::array<const char*, ItemMenuChanges::kMenus> cNames{"ContainerMenu", "BarterMenu", "InventoryMenu"};
std::array<std::atomic<IMenu*>, ItemMenuChanges::kMenus> s_open{};
// The holder each open menu lists beside the player, read on the UI's thread for the other threads' Note.
std::array<std::atomic<uint32_t>, ItemMenuChanges::kMenus> s_holder{};
std::atomic<uint32_t> s_player{};
ItemMenuChanges s_changes;
ItemMenuGate s_gate;
thread_local int t_changeDepth = 0;
thread_local int t_uiDepth = 0;

auto Elapsed() noexcept
{
    return [cStart = std::chrono::steady_clock::now()] {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cStart).count());
    };
}

void Pause() noexcept
{
    std::this_thread::yield();
}

uint32_t ListedHolder(Kind aKind) noexcept
{
    switch (aKind)
    {
    case Kind::Container:
    {
        POINTER_SKYRIMSE(uint32_t, s_target, 405962); // ContainerMenu's target (CommonLibSSE-NG GetTargetRefHandle)
        const auto* pTarget = TESObjectREFR::GetByHandle(*s_target.Get());
        return pTarget ? pTarget->formID : 0;
    }
    case Kind::Barter:
    {
        POINTER_SKYRIMSE(uint32_t, s_merchant, 405823); // BarterMenu's merchant (CommonLibSSE-NG GetTargetRefHandle)
        const auto* pMerchant = TESObjectREFR::GetByHandle(*s_merchant.Get());
        return pMerchant ? pMerchant->formID : 0;
    }
    case Kind::Inventory: return 0;
    }
    return 0;
}

// The menu's list and the count of items its next rebuild would re-read alone.
std::pair<ItemList*, uint32_t*> ListOf(Kind aKind, IMenu* apMenu) noexcept
{
    switch (aKind)
    {
    case Kind::Container: return {static_cast<ContainerMenu*>(apMenu)->pItemList, &static_cast<ContainerMenu*>(apMenu)->ChangedFormCount};
    case Kind::Barter: return {static_cast<BarterMenu*>(apMenu)->pItemList, &static_cast<BarterMenu*>(apMenu)->ChangedFormCount};
    case Kind::Inventory: return {static_cast<InventoryMenu*>(apMenu)->pItemList, &static_cast<InventoryMenu*>(apMenu)->ChangedFormCount};
    }
    return {};
}
} // namespace

void Shown(Kind aKind, IMenu* apMenu) noexcept
{
    s_holder[static_cast<size_t>(aKind)].store(ListedHolder(aKind), std::memory_order_release);
    s_open[static_cast<size_t>(aKind)].store(apMenu, std::memory_order_release);
}

void Hidden(Kind aKind) noexcept
{
    s_open[static_cast<size_t>(aKind)].store(nullptr, std::memory_order_release);
}

namespace
{
// Notes each open menu listing apHolder; whether any does.
bool Note(const TESObjectREFR* apHolder) noexcept
{
    bool listed = false;
    if (!apHolder)
        return listed;
    for (size_t i = 0; i < s_open.size(); ++i)
        if (s_open[i].load(std::memory_order_acquire))
        {
            const ItemMenuView cView{.Open = true,
                                     .Lists = {s_holder[i].load(std::memory_order_acquire), s_player.load(std::memory_order_acquire)},
                                     .AnyContainer = static_cast<Kind>(i) == Kind::Barter};
            if (MustRebuildFirst(cView, apHolder->formID, apHolder->formType != FormType::Character))
            {
                s_changes.Note(i);
                listed = true;
            }
        }
    return listed;
}

// Each noted open menu: its named items' count to 0, so the rebuild is the full one, then Update_Impl (selection hidden
// at once; rebuilt at the next 82082).
void RebuildNoted() noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    s_player.store(pPlayer ? pPlayer->formID : 0, std::memory_order_release);
    UI* pUI = UI::Get();
    for (size_t i = 0; i < s_open.size(); ++i)
    {
        const auto cKind = static_cast<Kind>(i);
        IMenu* pMenu = s_open[i].load(std::memory_order_acquire);
        // Still the UI's menu of that name: one gone without a hide message is dropped, never read.
        if (pMenu && (!pUI || pUI->FindMenuByName(cNames[i]) != pMenu))
        {
            Hidden(cKind);
            pMenu = nullptr;
        }
        if (pMenu)
            s_holder[i].store(ListedHolder(cKind), std::memory_order_release);
        if (!s_changes.Take(i, pMenu != nullptr) || !pPlayer)
            continue;
        const auto [pList, pChanged] = ListOf(cKind, pMenu);
        if (!pList)
            continue;
        // No items named: the rebuild reads the whole list, not only those (51144 -> 51146, 50958 -> 50960).
        *pChanged = 0;
        // The player as owner: every item menu's kInventoryUpdate takes it (403520; 51126, 50945, 51848).
        using TUpdate = void(ItemList*, TESObjectREFR*);
        POINTER_SKYRIMSE(TUpdate, s_update, 51031);
        s_update.Get()(pList, pPlayer);
    }
}
} // namespace

Change::Change(const TESObjectREFR* apHolder) noexcept
    : m_pHolder(apHolder)
{
    if (!Note(m_pHolder))
        return;
    m_counted = true;
    // Inside a listed change on this thread (the outer one holds the gate), or the UI's own (a menu's take, 51141):
    // it is the reader itself.
    if (t_changeDepth++ > 0 || t_uiDepth > 0)
        return;
    m_gated = true;
    if (!s_gate.RemovalBegin(Elapsed(), Pause))
        spdlog::warn("ItemMenuWatch: a change to {:X} went on while the UI read its menu ({} us)", m_pHolder->formID, ItemMenuGate::kWaitMicros);
}

Change::~Change()
{
    Note(m_pHolder);
    if (m_counted)
        --t_changeDepth;
    if (m_gated)
        s_gate.RemovalEnd();
}

UiCall::UiCall() noexcept
{
    if (t_uiDepth++ > 0)
        return;
    // A UI call inside this thread's own listed change waits for nobody: it would wait for itself.
    if (t_changeDepth > 0)
    {
        RebuildNoted();
        return;
    }
    m_gated = true;
    if (!s_gate.UiBegin(Elapsed(), Pause))
        spdlog::warn("ItemMenuWatch: the UI read its menus while a listed change still ran ({} us)", ItemMenuGate::kWaitMicros);
    RebuildNoted();
}

UiCall::~UiCall()
{
    --t_uiDepth;
    if (m_gated)
        s_gate.UiEnd();
}
} // namespace ItemMenuWatch
