#include <ScriptValues.h>

#include <Bridges/IFormBridge.h>
#include <SessionIds.h>
#include <FormIds.h>
#include <World.h>

namespace Utils
{
ScriptValue ToScriptValue(const LocalValue& acValue) noexcept
{
    const auto cToSession = [](const LocalForm& acForm) {
        ScriptForm form;
        if (const auto cSessionId = GetSessionIdByFormId(acForm.FormId))
            form.SessionId = *cSessionId;
        else
            form.Form = ToSession(acForm.FormId).value_or(GameId{});
        return std::optional(form);
    };
    return *MapForm<ScriptForm>(acValue, cToSession);
}

std::optional<LocalValue> ToLocalValue(const ScriptValue& acValue) noexcept
{
    const auto cToLocal = [](const ScriptForm& acForm) {
        const uint32_t cFormId = acForm.SessionId ? GetFormIdBySessionId(acForm.SessionId)
                                                  : ToGame(acForm.Form);
        return World::Get().GetFormBridge().FormExists(cFormId) ? std::optional(LocalForm{cFormId}) : std::nullopt;
    };
    return MapForm<LocalForm>(acValue, cToLocal);
}

ScriptCall ToScriptCall(const LocalCall& acCall) noexcept
{
    ScriptCall call{.Class = acCall.Class, .Function = acCall.Function, .Self = ToScriptValue(acCall.Self)};
    for (const auto& cArgument : acCall.Arguments)
        call.Arguments.push_back(ToScriptValue(cArgument));
    return call;
}

std::optional<LocalCall> ToLocalCall(const ScriptCall& acCall) noexcept
{
    auto self = ToLocalValue(acCall.Self);
    if (!self)
        return std::nullopt;
    LocalCall call{.Class = acCall.Class, .Function = acCall.Function, .Self = std::move(*self)};
    for (const auto& cArgument : acCall.Arguments)
    {
        auto argument = ToLocalValue(cArgument);
        if (!argument)
            return std::nullopt;
        call.Arguments.push_back(std::move(*argument));
    }
    return call;
}
} // namespace Utils
