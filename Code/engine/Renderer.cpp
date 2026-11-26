#include <Renderer.h>
#include <VersionDb.h>

BGSRenderer* BGSRenderer::Get()
{
    POINTER_SKYRIMSE(BGSRenderer*, s_instance, 411347);

    return *(s_instance.Get());
}

ID3D11Device* BGSRenderer::GetDevice()
{
    POINTER_SKYRIMSE(ID3D11Device*, s_device, 411348);

    return *(s_device.Get());
}
