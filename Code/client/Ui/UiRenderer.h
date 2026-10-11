#pragma once

#include <RmlUi/Core/RenderInterface.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11BlendState;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;

/**
 * @brief Draws RmlUi into the game's frame with Direct3D 11 (RmlUi ships no D3D11 renderer).
 *
 * Geometry, textures, scissor, transforms and gradient shaders (linear, radial and conic
 * decorators). Clip masks, layers and filters are left to RmlUi's defaults (not drawn).
 * BeginFrame/EndFrame wrap a frame: they save the game's pipeline state, draw into whatever render
 * target the game has bound (its back buffer at the end of the frame, as ImGui does) and restore it.
 */
struct UiRenderer final : Rml::RenderInterface
{
    UiRenderer(ID3D11Device* apDevice, ID3D11DeviceContext* apContext) noexcept;
    ~UiRenderer() override;

    [[nodiscard]] bool IsReady() const noexcept { return m_ready; }

    // Around the context's Render(): the game's state out, ours in, and back.
    void BeginFrame(int aWidth, int aHeight) noexcept;
    void EndFrame() noexcept;

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> aVertices, Rml::Span<const int> aIndices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle aGeometry, Rml::Vector2f aTranslation, Rml::TextureHandle aTexture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle aGeometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& aDimensions, const Rml::String& acSource) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> aSource, Rml::Vector2i aDimensions) override;
    void ReleaseTexture(Rml::TextureHandle aTexture) override;

    void EnableScissorRegion(bool aEnable) override;
    void SetScissorRegion(Rml::Rectanglei aRegion) override;
    void SetTransform(const Rml::Matrix4f* apTransform) override;

    Rml::CompiledShaderHandle CompileShader(const Rml::String& acName, const Rml::Dictionary& acParameters) override;
    void RenderShader(Rml::CompiledShaderHandle aShader, Rml::CompiledGeometryHandle aGeometry, Rml::Vector2f aTranslation,
                      Rml::TextureHandle aTexture) override;
    void ReleaseShader(Rml::CompiledShaderHandle aShader) override;

private:
    struct SavedState;

    bool CreateResources() noexcept;
    Rml::TextureHandle CreateTexture(const void* apPixels, int aWidth, int aHeight) noexcept;
    void UploadConstants(Rml::Vector2f aTranslation) noexcept;

    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pContext;
    bool m_ready = false;

    ID3D11VertexShader* m_pVertexShader = nullptr;
    ID3D11PixelShader* m_pPixelShader = nullptr;
    ID3D11PixelShader* m_pGradientShader = nullptr;
    ID3D11Buffer* m_pGradientConstants = nullptr;
    ID3D11InputLayout* m_pInputLayout = nullptr;
    ID3D11Buffer* m_pConstants = nullptr;
    ID3D11BlendState* m_pBlend = nullptr;
    ID3D11RasterizerState* m_pRasterizer = nullptr;
    ID3D11DepthStencilState* m_pDepth = nullptr;
    ID3D11SamplerState* m_pSampler = nullptr;
    // Untextured geometry samples this (one opaque white pixel), so one shader does both.
    Rml::TextureHandle m_white = 0;

    Rml::Matrix4f m_projection;
    Rml::Matrix4f m_transform;
    int m_width = 0;
    int m_height = 0;
    bool m_scissor = false;
    SavedState* m_pSaved = nullptr;
};
