#pragma once

#include <RmlUi_Renderer_GL3.h>

// Match the game's unsupported features using RmlUi's default no-op implementations.
struct WorkbenchRenderer final : RenderInterface_GL3
{
    Rml::TextureHandle LoadTexture(Rml::Vector2i& aDimensions, const Rml::String& acSource) override;
    void EnableClipMask(bool aEnable) override
    {
        Rml::RenderInterface::EnableClipMask(aEnable);
    }
    void RenderToClipMask(Rml::ClipMaskOperation aOperation, Rml::CompiledGeometryHandle aGeometry,
                          Rml::Vector2f aTranslation) override
    {
        Rml::RenderInterface::RenderToClipMask(aOperation, aGeometry, aTranslation);
    }
    Rml::LayerHandle PushLayer() override
    {
        return Rml::RenderInterface::PushLayer();
    }
    void CompositeLayers(Rml::LayerHandle aSource, Rml::LayerHandle aDestination, Rml::BlendMode aBlend,
                         Rml::Span<const Rml::CompiledFilterHandle> aFilters) override
    {
        Rml::RenderInterface::CompositeLayers(aSource, aDestination, aBlend, aFilters);
    }
    void PopLayer() override
    {
        Rml::RenderInterface::PopLayer();
    }
    Rml::TextureHandle SaveLayerAsTexture() override
    {
        return Rml::RenderInterface::SaveLayerAsTexture();
    }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override
    {
        return Rml::RenderInterface::SaveLayerAsMaskImage();
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& acName, const Rml::Dictionary& acParameters) override
    {
        return Rml::RenderInterface::CompileFilter(acName, acParameters);
    }
    void ReleaseFilter(Rml::CompiledFilterHandle aFilter) override
    {
        Rml::RenderInterface::ReleaseFilter(aFilter);
    }
    Rml::CompiledShaderHandle CompileShader(const Rml::String& acName, const Rml::Dictionary& acParameters) override
    {
        if (acName != "linear-gradient" && acName != "radial-gradient" && acName != "conic-gradient")
            return {};
        return RenderInterface_GL3::CompileShader(acName, acParameters);
    }
};
