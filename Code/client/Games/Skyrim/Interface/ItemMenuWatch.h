#pragma once

#include <cstdint>

struct IMenu;
struct TESObjectREFR;

// The open item menus list their holders' stacks by those stacks' own lists (docs/re/systems/item-menus.md). A change
// to a listed holder is noted on any thread; the UI job rebuilds the menu before its callbacks read the list.
namespace ItemMenuWatch
{
enum class Kind : uint8_t
{
    Container,
    Barter,
    Inventory,
};

// The menu's kShow (it lists from now on) or kHide / kForceHide (before the engine frees its list), on the UI job.
void Shown(Kind aKind, IMenu* apMenu) noexcept;
void Hidden(Kind aKind) noexcept;

// Before apHolder's stacks change, ours or the engine's (RemoveItem 16059, unequip 38934, outfit reset 418622,
// ResetInventory 37322): an open item menu listing it is noted. Any thread; reads no menu.
void RebuildFirst(const TESObjectREFR* apHolder) noexcept;

// Job_UI's start (39042), before its messages (82082) and movies (82083) run any menu callback: each noted open menu
// rebuilds its whole list (ItemList::Update_Impl 51031).
void OnUiJob() noexcept;
} // namespace ItemMenuWatch
