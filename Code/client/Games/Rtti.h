#pragma once

#include <cstddef>

// An object's class from its MSVC RTTI (the CompleteObjectLocator before its vtable). Never faults: every address is
// checked before it is read (our crash handler takes any access violation for a crash).
namespace Rtti
{
// "Actor" from ".?AVActor@@" into apOut; false when it is not (yet) an object of the game's.
bool ClassName(const void* apObject, char* apOut, size_t aSize) noexcept;
// Both at the cost of one check of the address (VirtualQuery: slow in a game this size).
bool ClassName(const void* apObject, char* apOut, size_t aSize, bool& aIsForm) noexcept;
// Whether TESForm is among its classes; false when it is not an object of the game's.
bool IsForm(const void* apObject) noexcept;
} // namespace Rtti
