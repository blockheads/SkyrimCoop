#pragma once

#include <cstdint>

// The players' characters in each save's SKSE co-save (SaveService; docs/design/join-flow.md, "The character record").
namespace CoSave
{
// Once, as the plugin loads: SKSE's serialization interface (kInterface_Serialization) and this plugin's handle.
void Start(void* apSerialization, uint32_t aPluginHandle, bool aFirstLoad) noexcept;
// Before a hot reload unloads this code: SKSE calls nothing of ours.
void Stop() noexcept;
} // namespace CoSave
