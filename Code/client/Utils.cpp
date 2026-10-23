#include <World.h>
#include <Structs/ScriptValue.h>
#include <Components.h>
#include <Actor.h>
#include <VersionDb.h>

namespace Utils
{

std::optional<uint32_t> GetSessionId(entt::entity aEntity) noexcept
{
    const auto* pLocalComponent = World::Get().try_get<LocalComponent>(aEntity);
    const auto* pRemoteComponent = World::Get().try_get<RemoteComponent>(aEntity);

    uint32_t sessionId = -1;
    if (pLocalComponent)
        sessionId = pLocalComponent->Id;
    else if (pRemoteComponent)
        sessionId = pRemoteComponent->Id;
    else
    {
        const auto* pFormIdComponent = World::Get().try_get<FormIdComponent>(aEntity);
        spdlog::debug("{}: This entity has neither a local or remote component: {:X}, form id: {:X}", __FUNCTION__, to_integral(aEntity), pFormIdComponent ? pFormIdComponent->Id : 0);
        return std::nullopt;
    }

    return {sessionId};
}

uint32_t GetFormIdBySessionId(uint32_t aSessionId) noexcept
{
    auto view = World::Get().view<FormIdComponent>();
    for (entt::entity entity : view)
        if (GetSessionId(entity) == aSessionId)
            return view.get<FormIdComponent>(entity).Id;
    spdlog::debug("GetFormIdBySessionId: form not found for session id {:X}", aSessionId);
    return 0;
}

std::optional<uint32_t> GetSessionIdByFormId(uint32_t aFormId) noexcept
{
    if (!aFormId)
        return std::nullopt;
    auto view = World::Get().view<FormIdComponent>();
    for (entt::entity entity : view)
        if (view.get<FormIdComponent>(entity).Id == aFormId)
            return GetSessionId(entity);
    return std::nullopt;
}

ScriptValue ToScriptValue(const LocalValue& acValue) noexcept
{
    const auto cToSession = [](const LocalForm& acForm) {
        ScriptForm form;
        if (const auto cSessionId = GetSessionIdByFormId(acForm.FormId))
            form.SessionId = *cSessionId;
        else
            World::Get().GetModSystem().GetServerModId(acForm.FormId, form.Form);
        return std::optional(form);
    };
    return *MapForm<ScriptForm>(acValue, cToSession);
}

std::optional<LocalValue> ToLocalValue(const ScriptValue& acValue) noexcept
{
    const auto cToLocal = [](const ScriptForm& acForm) {
        const uint32_t cFormId = acForm.SessionId ? GetFormIdBySessionId(acForm.SessionId) : World::Get().GetModSystem().GetGameId(acForm.Form);
        return TESForm::GetById(cFormId) ? std::optional(LocalForm{cFormId}) : std::nullopt;
    };
    return MapForm<LocalForm>(acValue, cToLocal);
}

TESForm* GetFormBySessionId(uint32_t aSessionId) noexcept
{
    if (const uint32_t cFormId = GetFormIdBySessionId(aSessionId))
        return TESForm::GetById(cFormId);
    return nullptr;
}

void ShowHudMessage(const std::string& acMessage)
{
    using TShowHudMessage = void(const char*, const char*, bool);

    POINTER_SKYRIMSE(TShowHudMessage, s_showHudMessage, 52933);

    s_showHudMessage(acMessage.c_str(), nullptr, false);
}

} // namespace Utils
