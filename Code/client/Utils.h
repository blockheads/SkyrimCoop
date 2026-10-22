#pragma once

#include <new>
#include <optional>
#include "VersionDb.h"
#include "World.h"

#define POINTER_SKYRIMSE(className, variableName, ...) static VersionDbPtr<className> variableName(__VA_ARGS__)
#define POINTER_SKYRIMSE_LEGACY(className, variableName, ...) static AutoPtr<decltype()> variableName(__VA_ARGS__)

// TODO: should this be debug only? I removed the check since debug is broken, can only use releasedbg
#define TP_ASSERT(Expr, Msg, ...)                                    \
    if (!(Expr))                                                     \
    {                                                                \
        Utils::Assert(#Expr, fmt::format(Msg, ##__VA_ARGS__).c_str()); \
    }

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

static void Assert(const char* apExpression, const char* apMessage)
{
    spdlog::critical("Assertion failed ({}): {}", apExpression, apMessage);

    if (IsDebuggerPresent())
        __debugbreak();
}

std::optional<uint32_t> GetSessionId(entt::entity aEntity) noexcept;

/// Returns the local formId for a given server-side actor id.
/// Does ECS-only lookup — no SKSE types or TESForm::GetById.
/// Returns 0 if not found.
inline uint32_t GetFormIdBySessionId(const uint32_t acSessionId) noexcept
{
    auto view = World::Get().view<FormIdComponent>();
    for (entt::entity entity : view)
    {
        std::optional<uint32_t> sessionIdRes = GetSessionId(entity);
        if (!sessionIdRes.has_value() || sessionIdRes.value() != acSessionId)
            continue;
        return view.get<FormIdComponent>(entity).Id;
    }
    spdlog::debug("GetFormIdBySessionId: form not found for session id {:X}", acSessionId);
    return 0;
}

/// The session id of the actor with this local formId, if the mod tracks it.
inline std::optional<uint32_t> GetSessionIdByFormId(const uint32_t acFormId) noexcept
{
    if (!acFormId)
        return std::nullopt;
    auto view = World::Get().view<FormIdComponent>();
    for (entt::entity entity : view)
        if (view.get<FormIdComponent>(entity).Id == acFormId)
            return GetSessionId(entity);
    return std::nullopt;
}

// GetBySessionId<T> requires the full TESForm type (TESForm::GetById) and RTTI Cast<T>.
// Only available when Games/Skyrim SKSE types are compiled in (i.e. HAS_DIRECTXTK builds).
template <class T> T* GetBySessionId(const uint32_t acSessionId) noexcept
{
    auto view = World::Get().view<FormIdComponent>();

    for (entt::entity entity : view)
    {
        std::optional<uint32_t> sessionIdRes = GetSessionId(entity);
        if (!sessionIdRes.has_value())
            continue;

        uint32_t sessionId = sessionIdRes.value();

        if (sessionId == acSessionId)
        {
            const auto& formIdComponent = view.get<FormIdComponent>(entity);
            TESForm* pForm = TESForm::GetById(formIdComponent.Id);

            if (pForm != nullptr)
            {
                return Cast<T>(pForm);
            }
        }
    }

    spdlog::debug("{}: form not found for session id {:X}", __FUNCTION__, acSessionId);
    return nullptr;
}

void ShowHudMessage(const std::string& acMessage);
} // namespace Utils
