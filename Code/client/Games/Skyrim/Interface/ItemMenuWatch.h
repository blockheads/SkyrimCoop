#pragma once

#include <cstdint>

struct IMenu;
struct TESObjectREFR;

// The open item menus list their holders' stacks by those stacks' own lists (docs/re/systems/item-menus.md). A change
// to a listed holder is noted on any thread; the UI's message pass rebuilds the menu before its callbacks read the list.
namespace ItemMenuWatch
{
enum class Kind : uint8_t
{
    Container,
    Barter,
    Inventory,
};

// The menu's kShow (it lists from now on) or kHide / kForceHide (before the engine frees its list), on the UI's thread.
void Shown(Kind aKind, IMenu* apMenu) noexcept;
void Hidden(Kind aKind) noexcept;

// Around a change to apHolder's stacks, ours or the engine's (RemoveItem 16059, unequip 38934, outfit reset 418622,
// ResetInventory 37322), on any thread: when an open item menu lists it, the change waits for the UI's call in
// progress (ItemMenuGate) and the menu is noted before and after it. Reads no engine memory but apHolder's id and type.
class Change
{
public:
    explicit Change(const TESObjectREFR* apHolder) noexcept;
    ~Change();
    Change(const Change&) = delete;
    Change& operator=(const Change&) = delete;

private:
    const TESObjectREFR* m_pHolder;
    bool m_counted{}; // listed: this thread's depth counts it
    bool m_gated{};   // the outermost listed change: holds the gate
};

// Around UI::ProcessMessages (82082) and the movies' advance (82083), where the menus' callbacks read their lists: waits
// for listed changes in flight, then each noted open menu is marked to rebuild its whole list (Update_Impl 51031).
class UiCall
{
public:
    UiCall() noexcept;
    ~UiCall();
    UiCall(const UiCall&) = delete;
    UiCall& operator=(const UiCall&) = delete;

private:
    bool m_gated{}; // the outermost UI call on this thread: holds the gate
};
} // namespace ItemMenuWatch
