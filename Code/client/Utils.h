#pragma once

#include <new>
#include <optional>
#include <source_location>
#include <string_view>
#include <RTTI.h>
#include <ScriptValues.h>
#include <SessionIds.h>

struct TESForm;

// Engine sub-objects we declare as members (actorValueOwner, animationGraphHolder,
// keywordForm) are base-class sub-objects in the engine, carrying the engine's vtables.
// Clang turns a call on a member of known type into a direct call (MSVC does not),
// which bypasses the engine's override. Call through this to keep virtual dispatch:
//   EngineVirtual(actorValueOwner).GetValue(id)
template <class T> [[nodiscard]] T& EngineVirtual(T& aObject) noexcept
{
    return *std::launder(&aObject);
}

namespace Utils
{

// Logs a broken expectation and breaks into an attached debugger; the caller goes on.
inline void Assert(bool aCondition, std::string_view aMessage, std::source_location aWhere = std::source_location::current()) noexcept
{
    if (aCondition)
        return;
    spdlog::critical("Assertion failed ({}:{}): {}", aWhere.file_name(), aWhere.line(), aMessage);
    if (IsDebuggerPresent())
        __debugbreak();
}

TESForm* GetFormBySessionId(uint32_t aSessionId) noexcept;

template <class T> T* GetBySessionId(uint32_t aSessionId) noexcept
{
    TESForm* pForm = GetFormBySessionId(aSessionId);
    return pForm ? Cast<T>(pForm) : nullptr;
}

void ShowHudMessage(const std::string& acMessage);
} // namespace Utils
