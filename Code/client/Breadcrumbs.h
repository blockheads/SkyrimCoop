#pragma once

#include <cstdint>

/**
 * @brief The last changes our code made to the engine, for crash reports. CrashLoggerSSE's log
 * shows what the engine was doing when it crashed; this shows what we did to it just before
 * (the crash handler writes it to tp_client.log).
 *
 * Every state-changing SkyrimBridge call records one, and so does every call of one of our hooks
 * (the engine calling into us: a crash after the engine unloaded something we hooked shows here).
 * A call repeating a recent one (the same change to the same form, as per-frame updates do) only
 * counts up there, so frequent calls do not push the rare ones out.
 */
namespace Breadcrumbs
{
// acpWhat must be a string literal. aDetail: a second form or value, when the call has one.
void Record(const char* acpWhat, uint32_t aFormId, uint32_t aDetail = 0) noexcept;
// One of our hooks ran (TiltedPhoques::SetHookObserver): its name and first argument (the object a
// member hook runs on), described as it is now.
void RecordHook(const char* acpHookName, const void* apFirstArgument) noexcept;
// Whether to observe a hook at all: not the ones called thousands of times a frame.
bool ObservesHook(const char* acpHookName) noexcept;
// Into the log, oldest first, times relative to now. Safe to call from the crash handler.
void Dump() noexcept;
} // namespace Breadcrumbs
