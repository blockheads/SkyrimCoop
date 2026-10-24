#include "WorkbenchRenderer.h"

#include <RmlUi/Core/Log.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

Rml::TextureHandle WorkbenchRenderer::LoadTexture(Rml::Vector2i& aDimensions, const Rml::String& acSource)
{
    if (!acSource.ends_with(".png"))
        return RenderInterface_GL3::LoadTexture(aDimensions, acSource);
    int channels = 0;
    auto* pPixels = stbi_load(acSource.c_str(), &aDimensions.x, &aDimensions.y, &channels, 4);
    if (!pPixels)
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Could not load PNG: %s", acSource.c_str());
        return {};
    }
    const size_t count = size_t(aDimensions.x) * aDimensions.y * 4;
    for (size_t i = 0; i < count; i += 4)
        for (size_t channel = 0; channel < 3; ++channel)
            pPixels[i + channel] = (unsigned(pPixels[i + channel]) * pPixels[i + 3] + 127) / 255;
    const auto texture = GenerateTexture({pPixels, count}, aDimensions);
    stbi_image_free(pPixels);
    return texture;
}
