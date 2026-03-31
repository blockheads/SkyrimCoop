#pragma once

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

namespace Utils
{

static void Assert(const char* apExpression, const char* apMessage)
{
    spdlog::critical("Assertion failed ({}): {}", apExpression, apMessage);

    if (IsDebuggerPresent())
        __debugbreak();
}

std::optional<uint32_t> GetServerId(entt::entity aEntity) noexcept;

/// Returns the local formId for a given server-side actor id.
/// Does ECS-only lookup — no SKSE types or TESForm::GetById.
/// Returns 0 if not found.
inline uint32_t GetFormIdByServerId(const uint32_t acServerId) noexcept
{
    auto view = World::Get().view<FormIdComponent>();
    for (entt::entity entity : view)
    {
        std::optional<uint32_t> serverIdRes = GetServerId(entity);
        if (!serverIdRes.has_value() || serverIdRes.value() != acServerId)
            continue;
        return view.get<FormIdComponent>(entity).Id;
    }
    spdlog::debug("GetFormIdByServerId: form not found for server id {:X}", acServerId);
    return 0;
}

// GetByServerId<T> requires the full TESForm type (TESForm::GetById) and RTTI Cast<T>.
// Only available when Games/Skyrim SKSE types are compiled in (i.e. HAS_DIRECTXTK builds).
#ifdef HAS_DIRECTXTK
template <class T> T* GetByServerId(const uint32_t acServerId) noexcept
{
    auto view = World::Get().view<FormIdComponent>();

    for (entt::entity entity : view)
    {
        std::optional<uint32_t> serverIdRes = GetServerId(entity);
        if (!serverIdRes.has_value())
            continue;

        uint32_t serverId = serverIdRes.value();

        if (serverId == acServerId)
        {
            const auto& formIdComponent = view.get<FormIdComponent>(entity);
            TESForm* pForm = TESForm::GetById(formIdComponent.Id);

            if (pForm != nullptr)
            {
                return Cast<T>(pForm);
            }
        }
    }

    spdlog::debug("{}: form not found for server id {:X}", __FUNCTION__, acServerId);
    return nullptr;
}
#endif  // HAS_DIRECTXTK

void ShowHudMessage(const TiltedPhoques::String& acMessage);
} // namespace Utils

namespace TiltedPhoques
{
template <class TFunc, class TThis, class... TArgs> constexpr decltype(auto) ThisCall(TFunc* aFunction, VersionDbPtr<TThis>& aThis, TArgs&&... args) noexcept
{
    return ThisCall(aFunction, aThis.Get(), args...);
}

template <class TFunc, class TThis, class... TArgs> constexpr decltype(auto) ThisCall(VersionDbPtr<TFunc>& aFunction, VersionDbPtr<TThis>& aThis, TArgs&&... args) noexcept
{
    return ThisCall(aFunction.Get(), aThis.Get(), args...);
}

template <class TFunc, class TThis, class... TArgs> constexpr decltype(auto) ThisCall(VersionDbPtr<TFunc>& aFunction, TThis* apThis, TArgs&&... args) noexcept
{
    return ThisCall(aFunction.Get(), apThis, args...);
}
} // namespace TiltedPhoques
