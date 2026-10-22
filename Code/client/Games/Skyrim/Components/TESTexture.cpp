#include <TiltedOnlinePCH.h>

#include <Components/TESTexture.h>

void TESTexture::Construct()
{
    using TConstructor = TESTexture*(TESTexture*);

    POINTER_SKYRIMSE(TConstructor, s_constructor, 14953);

    s_constructor(this);
}
