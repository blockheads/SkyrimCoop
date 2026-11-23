#pragma once

#include <cstdint>

struct IMenu;
struct TESBoundObject;
struct TESObjectREFR;

// The open item menus list their holders' stacks by those stacks' own lists (docs/re/systems/item-menus.md). Their
// ProcessMessage hooks say when each shows and hides; any change to a listed holder's stacks marks the menu first.
namespace ItemMenuWatch
{
enum class Kind : uint8_t
{
    Container,
    Barter,
    Inventory,
};

// The menu's kShow (it lists from now on) or kHide / kForceHide (before the engine frees its list).
void Shown(Kind aKind, IMenu* apMenu) noexcept;
void Hidden(Kind aKind) noexcept;

// Before apHolder's stacks of apItem change (none: of anything it holds), ours or the engine's own (RemoveItem 16059,
// outfit reset 418622, ResetInventory 37322): an open item menu listing it re-reads them, as after the engine's take
// (each item's update 52849, then ItemList::Update_Impl 51031). Any thread.
void RebuildFirst(const TESObjectREFR* apHolder, const TESBoundObject* apItem = nullptr) noexcept;
} // namespace ItemMenuWatch
