#include <Components/TESAIForm.h>

#include <VersionDb.h>

uint8_t TESAIForm::GetConfidence() const noexcept
{
    using TGetConfidence = uint8_t(const TESAIForm*);
    POINTER_SKYRIMSE(TGetConfidence, s_getConfidence, 14457);
    return s_getConfidence(this);
}
