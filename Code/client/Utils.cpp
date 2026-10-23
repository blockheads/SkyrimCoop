#include <Utils.h>
#include <World.h>
#include <Structs/ScriptValue.h>
#include <Components.h>
#include <Actor.h>
#include <VersionDb.h>

namespace Utils
{

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
