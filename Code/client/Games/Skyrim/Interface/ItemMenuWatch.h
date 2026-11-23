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

// apHolder's stacks change, ours or the engine's (RemoveItem 16059, unequip 38934, outfit reset 418622,
// ResetInventory 37322): an open item menu listing it is noted. Called before and after the change (a UI pass between
// takes the first). Any thread; reads no engine memory but apHolder's id.
void Note(const TESObjectREFR* apHolder) noexcept;

// The start of UI::ProcessMessages (82082), which every caller runs before the movies' advance (82083): each noted
// open menu rebuilds its whole list (ItemList::Update_Impl 51031) before a callback can read it.
void BeforeUiMessages() noexcept;
} // namespace ItemMenuWatch
