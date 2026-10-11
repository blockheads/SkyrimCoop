#include <TiltedOnlinePCH.h>

#include <Ui/UiRenderer.h>

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Variant.h>
#include <RmlUi/Core/Vertex.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>

namespace
{
// RmlUi's vertices: position, premultiplied colour, texture coordinate. The transform is
// RmlUi's column-major matrix (HLSL's default packing), applied after the translation.
constexpr char cShaders[] = R"(
cbuffer Constants : register(b0)
{
    float4x4 Transform;
    float2 Translation;
};
struct VertexIn { float2 Position : POSITION; float4 Colour : COLOR0; float2 Uv : TEXCOORD0; };
struct PixelIn { float4 Position : SV_POSITION; float4 Colour : COLOR0; float2 Uv : TEXCOORD0; };
Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

PixelIn VS(VertexIn aIn)
{
    PixelIn result;
    result.Position = mul(Transform, float4(aIn.Position + Translation, 0, 1));
    result.Colour = aIn.Colour;
    result.Uv = aIn.Uv;
    return result;
}

float4 PS(PixelIn aIn) : SV_Target
{
    return aIn.Colour * Texture.Sample(Sampler, aIn.Uv);
}

// RmlUi's gradient decorators (its GL3 renderer's shader). The geometry's texture coordinates are
// its pixel positions.
#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
cbuffer Gradient : register(b1)
{
    int Function;
    int StopCount;
    float2 P; // linear: start point; radial, conic: centre
    float2 V; // linear: start to end; radial: inverse radius; conic: angled unit vector
    float2 Pad;
    float4 StopColours[16];
    float4 StopPositions[4]; // 16 positions, 0 at the start point, 1 at the end
};

float StopPosition(int aIndex) { return StopPositions[aIndex / 4][aIndex % 4]; }

float4 PSGradient(PixelIn aIn) : SV_Target
{
    float t = 0;
    if (Function == LINEAR || Function == REPEATING_LINEAR)
        t = dot(V, aIn.Uv - P) / dot(V, V);
    else if (Function == RADIAL || Function == REPEATING_RADIAL)
        t = length(V * (aIn.Uv - P));
    else
    {
        float2 d = aIn.Uv - P;
        float2 r = float2(V.x * d.x + V.y * d.y, -V.y * d.x + V.x * d.y);
        t = 0.5 + atan2(-r.x, r.y) / (2 * 3.14159265);
    }
    if (Function >= REPEATING_LINEAR)
    {
        float t0 = StopPosition(0);
        float range = StopPosition(StopCount - 1) - t0;
        float x = t - t0;
        t = t0 + (x - range * floor(x / range));
    }

    float4 colour = StopColours[0];
    for (int i = 1; i < StopCount; i++)
        colour = lerp(colour, StopColours[i], smoothstep(StopPosition(i - 1), StopPosition(i), t));
    return aIn.Colour * colour;
}
)";

enum class GradientFunction : int32_t
{
    Linear,
    Radial,
    Conic,
    RepeatingLinear,
    RepeatingRadial,
    RepeatingConic
};

constexpr int cMaxStops = 16;

struct GradientConstants
{
    int32_t Function;
    int32_t StopCount;
    float P[2];
    float V[2];
    float Pad[2];
    float StopColours[cMaxStops][4];
    float StopPositions[cMaxStops];
};
static_assert(sizeof(GradientConstants) % 16 == 0);

struct Constants
{
    float Transform[16];
    float Translation[2];
    float Pad[2];
};
static_assert(sizeof(Constants) % 16 == 0);

struct Geometry
{
    ID3D11Buffer* pVertices = nullptr;
    ID3D11Buffer* pIndices = nullptr;
    UINT IndexCount = 0;
};

struct Texture
{
    ID3D11Texture2D* pTexture = nullptr;
    ID3D11ShaderResourceView* pView = nullptr;
};

template <class T> void SafeRelease(T*& apObject) noexcept
{
    if (apObject)
        apObject->Release();
    apObject = nullptr;
}

ID3DBlob* Compile(const char* apEntry, const char* apTarget) noexcept
{
    ID3DBlob* pCode = nullptr;
    ID3DBlob* pErrors = nullptr;
    if (FAILED(D3DCompile(cShaders, sizeof(cShaders) - 1, "UiRenderer", nullptr, nullptr, apEntry, apTarget, 0, 0, &pCode, &pErrors)))
    {
        spdlog::error("UiRenderer: {} does not compile: {}", apEntry, pErrors ? static_cast<const char*>(pErrors->GetBufferPointer()) : "?");
        SafeRelease(pErrors);
        return nullptr;
    }
    SafeRelease(pErrors);
    return pCode;
}

// An image file as premultiplied RGBA8, through the Windows Imaging Component (PNG, JPEG, BMP,
// DDS, ... ; Wine implements it).
bool DecodeImage(const Rml::String& acPath, std::vector<uint8_t>& aPixels, UINT& aWidth, UINT& aHeight) noexcept
{
    const bool cUninitialize = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    IWICImagingFactory* pFactory = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICFormatConverter* pConverter = nullptr;
    bool decoded = false;

    const int cLength = MultiByteToWideChar(CP_UTF8, 0, acPath.c_str(), -1, nullptr, 0);
    std::wstring path(cLength > 0 ? cLength - 1 : 0, L'\0');
    if (cLength > 0)
        MultiByteToWideChar(CP_UTF8, 0, acPath.c_str(), -1, path.data(), cLength);

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))) &&
        SUCCEEDED(pFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder)) &&
        SUCCEEDED(pDecoder->GetFrame(0, &pFrame)) && SUCCEEDED(pFactory->CreateFormatConverter(&pConverter)) &&
        SUCCEEDED(pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(pConverter->GetSize(&aWidth, &aHeight)))
    {
        aPixels.resize(size_t(aWidth) * aHeight * 4);
        decoded = SUCCEEDED(pConverter->CopyPixels(nullptr, aWidth * 4, static_cast<UINT>(aPixels.size()), aPixels.data()));
    }

    SafeRelease(pConverter);
    SafeRelease(pFrame);
    SafeRelease(pDecoder);
    SafeRelease(pFactory);
    if (cUninitialize)
        CoUninitialize();
    return decoded;
}
} // namespace

// The game's pipeline state that drawing touches (as ImGui's D3D11 backend saves it).
struct UiRenderer::SavedState
{
    UINT ScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    UINT ViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT Scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    D3D11_VIEWPORT Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    ID3D11RasterizerState* pRasterizer = nullptr;
    ID3D11BlendState* pBlend = nullptr;
    FLOAT BlendFactor[4]{};
    UINT SampleMask = 0;
    UINT StencilRef = 0;
    ID3D11DepthStencilState* pDepth = nullptr;
    ID3D11ShaderResourceView* pShaderResource = nullptr;
    ID3D11SamplerState* pSampler = nullptr;
    ID3D11PixelShader* pPixelShader = nullptr;
    ID3D11VertexShader* pVertexShader = nullptr;
    ID3D11GeometryShader* pGeometryShader = nullptr;
    ID3D11ClassInstance* PixelInstances[256]{};
    ID3D11ClassInstance* VertexInstances[256]{};
    ID3D11ClassInstance* GeometryInstances[256]{};
    UINT PixelInstanceCount = 256, VertexInstanceCount = 256, GeometryInstanceCount = 256;
    D3D11_PRIMITIVE_TOPOLOGY Topology{};
    ID3D11Buffer* pIndexBuffer = nullptr;
    ID3D11Buffer* pVertexBuffer = nullptr;
    ID3D11Buffer* pVertexConstants = nullptr;
    ID3D11Buffer* pPixelConstants = nullptr;
    UINT IndexOffset = 0, VertexStride = 0, VertexOffset = 0;
    DXGI_FORMAT IndexFormat{};
    ID3D11InputLayout* pInputLayout = nullptr;
};

UiRenderer::UiRenderer(ID3D11Device* apDevice, ID3D11DeviceContext* apContext) noexcept
    : m_pDevice(apDevice)
    , m_pContext(apContext)
{
    m_pDevice->AddRef();
    m_pContext->AddRef();
    m_ready = CreateResources();
    if (m_ready)
    {
        constexpr uint32_t cWhite = 0xFFFFFFFF;
        m_white = CreateTexture(&cWhite, 1, 1);
    }
}

UiRenderer::~UiRenderer()
{
    ReleaseTexture(m_white);
    SafeRelease(m_pSampler);
    SafeRelease(m_pDepth);
    SafeRelease(m_pRasterizer);
    SafeRelease(m_pBlend);
    SafeRelease(m_pConstants);
    SafeRelease(m_pInputLayout);
    SafeRelease(m_pGradientConstants);
    SafeRelease(m_pGradientShader);
    SafeRelease(m_pPixelShader);
    SafeRelease(m_pVertexShader);
    SafeRelease(m_pContext);
    SafeRelease(m_pDevice);
    delete m_pSaved;
}

bool UiRenderer::CreateResources() noexcept
{
    ID3DBlob* pVertexCode = Compile("VS", "vs_4_0");
    ID3DBlob* pPixelCode = Compile("PS", "ps_4_0");
    ID3DBlob* pGradientCode = Compile("PSGradient", "ps_4_0");
    bool created = pVertexCode && pPixelCode && pGradientCode;
    if (created)
    {
        const D3D11_INPUT_ELEMENT_DESC cLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Rml::Vertex, colour), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, tex_coord), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        created = SUCCEEDED(m_pDevice->CreateVertexShader(pVertexCode->GetBufferPointer(), pVertexCode->GetBufferSize(), nullptr, &m_pVertexShader)) &&
                  SUCCEEDED(m_pDevice->CreatePixelShader(pPixelCode->GetBufferPointer(), pPixelCode->GetBufferSize(), nullptr, &m_pPixelShader)) &&
                  SUCCEEDED(m_pDevice->CreatePixelShader(pGradientCode->GetBufferPointer(), pGradientCode->GetBufferSize(), nullptr, &m_pGradientShader)) &&
                  SUCCEEDED(m_pDevice->CreateInputLayout(cLayout, 3, pVertexCode->GetBufferPointer(), pVertexCode->GetBufferSize(), &m_pInputLayout));
    }
    SafeRelease(pVertexCode);
    SafeRelease(pPixelCode);
    SafeRelease(pGradientCode);
    if (!created)
        return false;

    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = sizeof(Constants);
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    // Premultiplied alpha, as RmlUi produces it.
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.ScissorEnable = TRUE;
    rasterizer.DepthClipEnable = TRUE;

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.StencilEnable = FALSE;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;

    D3D11_BUFFER_DESC gradientConstants = constants;
    gradientConstants.ByteWidth = sizeof(GradientConstants);

    return SUCCEEDED(m_pDevice->CreateBuffer(&constants, nullptr, &m_pConstants)) &&
           SUCCEEDED(m_pDevice->CreateBuffer(&gradientConstants, nullptr, &m_pGradientConstants)) && SUCCEEDED(m_pDevice->CreateBlendState(&blend, &m_pBlend)) &&
           SUCCEEDED(m_pDevice->CreateRasterizerState(&rasterizer, &m_pRasterizer)) && SUCCEEDED(m_pDevice->CreateDepthStencilState(&depth, &m_pDepth)) &&
           SUCCEEDED(m_pDevice->CreateSamplerState(&sampler, &m_pSampler));
}

void UiRenderer::BeginFrame(int aWidth, int aHeight) noexcept
{
    m_width = aWidth;
    m_height = aHeight;
    m_projection = Rml::Matrix4f::ProjectOrtho(0, float(aWidth), float(aHeight), 0, -10000, 10000);
    m_transform = m_projection;
    m_scissor = false;

    if (!m_pSaved)
        m_pSaved = new SavedState;
    auto& s = *m_pSaved;
    s = SavedState{};
    m_pContext->RSGetScissorRects(&s.ScissorCount, s.Scissors);
    m_pContext->RSGetViewports(&s.ViewportCount, s.Viewports);
    m_pContext->RSGetState(&s.pRasterizer);
    m_pContext->OMGetBlendState(&s.pBlend, s.BlendFactor, &s.SampleMask);
    m_pContext->OMGetDepthStencilState(&s.pDepth, &s.StencilRef);
    m_pContext->PSGetShaderResources(0, 1, &s.pShaderResource);
    m_pContext->PSGetSamplers(0, 1, &s.pSampler);
    m_pContext->PSGetShader(&s.pPixelShader, s.PixelInstances, &s.PixelInstanceCount);
    m_pContext->VSGetShader(&s.pVertexShader, s.VertexInstances, &s.VertexInstanceCount);
    m_pContext->VSGetConstantBuffers(0, 1, &s.pVertexConstants);
    m_pContext->PSGetConstantBuffers(1, 1, &s.pPixelConstants);
    m_pContext->GSGetShader(&s.pGeometryShader, s.GeometryInstances, &s.GeometryInstanceCount);
    m_pContext->IAGetPrimitiveTopology(&s.Topology);
    m_pContext->IAGetIndexBuffer(&s.pIndexBuffer, &s.IndexFormat, &s.IndexOffset);
    m_pContext->IAGetVertexBuffers(0, 1, &s.pVertexBuffer, &s.VertexStride, &s.VertexOffset);
    m_pContext->IAGetInputLayout(&s.pInputLayout);

    D3D11_VIEWPORT viewport{};
    viewport.Width = float(aWidth);
    viewport.Height = float(aHeight);
    viewport.MaxDepth = 1.f;
    m_pContext->RSSetViewports(1, &viewport);
    const D3D11_RECT cFull{0, 0, aWidth, aHeight};
    m_pContext->RSSetScissorRects(1, &cFull);
    m_pContext->RSSetState(m_pRasterizer);
    const float cBlendFactor[4]{};
    m_pContext->OMSetBlendState(m_pBlend, cBlendFactor, 0xFFFFFFFF);
    m_pContext->OMSetDepthStencilState(m_pDepth, 0);
    m_pContext->IASetInputLayout(m_pInputLayout);
    m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pContext->VSSetShader(m_pVertexShader, nullptr, 0);
    m_pContext->VSSetConstantBuffers(0, 1, &m_pConstants);
    m_pContext->PSSetShader(m_pPixelShader, nullptr, 0);
    m_pContext->PSSetSamplers(0, 1, &m_pSampler);
    m_pContext->GSSetShader(nullptr, nullptr, 0);
}

void UiRenderer::EndFrame() noexcept
{
    if (!m_pSaved)
        return;
    auto& s = *m_pSaved;
    m_pContext->RSSetScissorRects(s.ScissorCount, s.Scissors);
    m_pContext->RSSetViewports(s.ViewportCount, s.Viewports);
    m_pContext->RSSetState(s.pRasterizer);
    m_pContext->OMSetBlendState(s.pBlend, s.BlendFactor, s.SampleMask);
    m_pContext->OMSetDepthStencilState(s.pDepth, s.StencilRef);
    m_pContext->PSSetShaderResources(0, 1, &s.pShaderResource);
    m_pContext->PSSetSamplers(0, 1, &s.pSampler);
    m_pContext->PSSetShader(s.pPixelShader, s.PixelInstances, s.PixelInstanceCount);
    m_pContext->VSSetShader(s.pVertexShader, s.VertexInstances, s.VertexInstanceCount);
    m_pContext->VSSetConstantBuffers(0, 1, &s.pVertexConstants);
    m_pContext->PSSetConstantBuffers(1, 1, &s.pPixelConstants);
    m_pContext->GSSetShader(s.pGeometryShader, s.GeometryInstances, s.GeometryInstanceCount);
    m_pContext->IASetPrimitiveTopology(s.Topology);
    m_pContext->IASetIndexBuffer(s.pIndexBuffer, s.IndexFormat, s.IndexOffset);
    m_pContext->IASetVertexBuffers(0, 1, &s.pVertexBuffer, &s.VertexStride, &s.VertexOffset);
    m_pContext->IASetInputLayout(s.pInputLayout);

    SafeRelease(s.pRasterizer);
    SafeRelease(s.pBlend);
    SafeRelease(s.pDepth);
    SafeRelease(s.pShaderResource);
    SafeRelease(s.pSampler);
    SafeRelease(s.pPixelShader);
    for (UINT i = 0; i < s.PixelInstanceCount; ++i)
        SafeRelease(s.PixelInstances[i]);
    SafeRelease(s.pVertexShader);
    for (UINT i = 0; i < s.VertexInstanceCount; ++i)
        SafeRelease(s.VertexInstances[i]);
    SafeRelease(s.pVertexConstants);
    SafeRelease(s.pPixelConstants);
    SafeRelease(s.pGeometryShader);
    for (UINT i = 0; i < s.GeometryInstanceCount; ++i)
        SafeRelease(s.GeometryInstances[i]);
    SafeRelease(s.pIndexBuffer);
    SafeRelease(s.pVertexBuffer);
    SafeRelease(s.pInputLayout);
}

Rml::CompiledGeometryHandle UiRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> aVertices, Rml::Span<const int> aIndices)
{
    auto* pGeometry = new Geometry;
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_IMMUTABLE;

    desc.ByteWidth = static_cast<UINT>(aVertices.size() * sizeof(Rml::Vertex));
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertices{aVertices.data()};

    D3D11_BUFFER_DESC indexDesc = desc;
    indexDesc.ByteWidth = static_cast<UINT>(aIndices.size() * sizeof(int));
    indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA indices{aIndices.data()};

    if (aVertices.empty() || aIndices.empty() || FAILED(m_pDevice->CreateBuffer(&desc, &vertices, &pGeometry->pVertices)) ||
        FAILED(m_pDevice->CreateBuffer(&indexDesc, &indices, &pGeometry->pIndices)))
    {
        ReleaseGeometry(reinterpret_cast<Rml::CompiledGeometryHandle>(pGeometry));
        return 0;
    }
    pGeometry->IndexCount = static_cast<UINT>(aIndices.size());
    return reinterpret_cast<Rml::CompiledGeometryHandle>(pGeometry);
}

void UiRenderer::RenderGeometry(Rml::CompiledGeometryHandle aGeometry, Rml::Vector2f aTranslation, Rml::TextureHandle aTexture)
{
    const auto* pGeometry = reinterpret_cast<const Geometry*>(aGeometry);
    const auto* pTexture = reinterpret_cast<const Texture*>(aTexture ? aTexture : m_white);
    if (!pGeometry || !pTexture)
        return;

    UploadConstants(aTranslation);
    constexpr UINT cStride = sizeof(Rml::Vertex);
    constexpr UINT cOffset = 0;
    m_pContext->IASetVertexBuffers(0, 1, &pGeometry->pVertices, &cStride, &cOffset);
    m_pContext->IASetIndexBuffer(pGeometry->pIndices, DXGI_FORMAT_R32_UINT, 0);
    m_pContext->PSSetShaderResources(0, 1, &pTexture->pView);
    m_pContext->DrawIndexed(pGeometry->IndexCount, 0, 0);
}

void UiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle aGeometry)
{
    auto* pGeometry = reinterpret_cast<Geometry*>(aGeometry);
    if (!pGeometry)
        return;
    SafeRelease(pGeometry->pVertices);
    SafeRelease(pGeometry->pIndices);
    delete pGeometry;
}

Rml::TextureHandle UiRenderer::LoadTexture(Rml::Vector2i& aDimensions, const Rml::String& acSource)
{
    std::vector<uint8_t> pixels;
    UINT width = 0, height = 0;
    if (!DecodeImage(acSource, pixels, width, height))
    {
        spdlog::warn("UiRenderer: cannot load image {}", acSource);
        return 0;
    }
    aDimensions = {int(width), int(height)};
    return CreateTexture(pixels.data(), int(width), int(height));
}

Rml::TextureHandle UiRenderer::GenerateTexture(Rml::Span<const Rml::byte> aSource, Rml::Vector2i aDimensions)
{
    return CreateTexture(aSource.data(), aDimensions.x, aDimensions.y);
}

Rml::TextureHandle UiRenderer::CreateTexture(const void* apPixels, int aWidth, int aHeight) noexcept
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = UINT(aWidth);
    desc.Height = UINT(aHeight);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA cData{apPixels, UINT(aWidth) * 4, 0};

    auto* pTexture = new Texture;
    if (FAILED(m_pDevice->CreateTexture2D(&desc, &cData, &pTexture->pTexture)) ||
        FAILED(m_pDevice->CreateShaderResourceView(pTexture->pTexture, nullptr, &pTexture->pView)))
    {
        ReleaseTexture(reinterpret_cast<Rml::TextureHandle>(pTexture));
        return 0;
    }
    return reinterpret_cast<Rml::TextureHandle>(pTexture);
}

void UiRenderer::ReleaseTexture(Rml::TextureHandle aTexture)
{
    auto* pTexture = reinterpret_cast<Texture*>(aTexture);
    if (!pTexture)
        return;
    SafeRelease(pTexture->pView);
    SafeRelease(pTexture->pTexture);
    delete pTexture;
}

void UiRenderer::EnableScissorRegion(bool aEnable)
{
    m_scissor = aEnable;
    if (!aEnable)
    {
        const D3D11_RECT cFull{0, 0, m_width, m_height};
        m_pContext->RSSetScissorRects(1, &cFull);
    }
}

void UiRenderer::SetScissorRegion(Rml::Rectanglei aRegion)
{
    if (!m_scissor)
        return;
    const D3D11_RECT cRect{aRegion.Left(), aRegion.Top(), aRegion.Right(), aRegion.Bottom()};
    m_pContext->RSSetScissorRects(1, &cRect);
}

void UiRenderer::SetTransform(const Rml::Matrix4f* apTransform)
{
    m_transform = apTransform ? m_projection * *apTransform : m_projection;
}

void UiRenderer::UploadConstants(Rml::Vector2f aTranslation) noexcept
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_pContext->Map(m_pConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    auto* pConstants = static_cast<Constants*>(mapped.pData);
    std::memcpy(pConstants->Transform, m_transform.data(), sizeof(pConstants->Transform));
    pConstants->Translation[0] = aTranslation.x;
    pConstants->Translation[1] = aTranslation.y;
    m_pContext->Unmap(m_pConstants, 0);
}

Rml::CompiledShaderHandle UiRenderer::CompileShader(const Rml::String& acName, const Rml::Dictionary& acParameters)
{
    auto* pGradient = new GradientConstants{};
    const bool cRepeating = Rml::Get(acParameters, "repeating", false);
    Rml::Vector2f p{}, v{};
    if (acName == "linear-gradient")
    {
        pGradient->Function = int32_t(cRepeating ? GradientFunction::RepeatingLinear : GradientFunction::Linear);
        p = Rml::Get(acParameters, "p0", Rml::Vector2f(0.f));
        v = Rml::Get(acParameters, "p1", Rml::Vector2f(0.f)) - p;
    }
    else if (acName == "radial-gradient")
    {
        pGradient->Function = int32_t(cRepeating ? GradientFunction::RepeatingRadial : GradientFunction::Radial);
        p = Rml::Get(acParameters, "center", Rml::Vector2f(0.f));
        v = Rml::Vector2f(1.f) / Rml::Get(acParameters, "radius", Rml::Vector2f(1.f));
    }
    else if (acName == "conic-gradient")
    {
        pGradient->Function = int32_t(cRepeating ? GradientFunction::RepeatingConic : GradientFunction::Conic);
        p = Rml::Get(acParameters, "center", Rml::Vector2f(0.f));
        const float cAngle = Rml::Get(acParameters, "angle", 0.f);
        v = {std::cos(cAngle), std::sin(cAngle)};
    }
    else
    {
        spdlog::warn("UiRenderer: no shader \"{}\"", acName);
        delete pGradient;
        return 0;
    }
    pGradient->P[0] = p.x;
    pGradient->P[1] = p.y;
    pGradient->V[0] = v.x;
    pGradient->V[1] = v.y;

    const auto cStops = acParameters.find("color_stop_list");
    if (cStops != acParameters.end() && cStops->second.GetType() == Rml::Variant::COLORSTOPLIST)
    {
        const auto& stops = cStops->second.GetReference<Rml::ColorStopList>();
        pGradient->StopCount = std::min(int(stops.size()), cMaxStops);
        for (int i = 0; i < pGradient->StopCount; ++i)
        {
            pGradient->StopPositions[i] = stops[i].position.number;
            for (int c = 0; c < 4; ++c)
                pGradient->StopColours[i][c] = stops[i].color[c] / 255.f; // premultiplied
        }
    }
    return reinterpret_cast<Rml::CompiledShaderHandle>(pGradient);
}

void UiRenderer::RenderShader(Rml::CompiledShaderHandle aShader, Rml::CompiledGeometryHandle aGeometry, Rml::Vector2f aTranslation,
                              Rml::TextureHandle)
{
    const auto* pGradient = reinterpret_cast<const GradientConstants*>(aShader);
    const auto* pGeometry = reinterpret_cast<const Geometry*>(aGeometry);
    if (!pGradient || !pGeometry || pGradient->StopCount == 0)
        return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_pContext->Map(m_pGradientConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, pGradient, sizeof(GradientConstants));
    m_pContext->Unmap(m_pGradientConstants, 0);

    UploadConstants(aTranslation);
    constexpr UINT cStride = sizeof(Rml::Vertex);
    constexpr UINT cOffset = 0;
    m_pContext->IASetVertexBuffers(0, 1, &pGeometry->pVertices, &cStride, &cOffset);
    m_pContext->IASetIndexBuffer(pGeometry->pIndices, DXGI_FORMAT_R32_UINT, 0);
    m_pContext->PSSetShader(m_pGradientShader, nullptr, 0);
    m_pContext->PSSetConstantBuffers(1, 1, &m_pGradientConstants);
    m_pContext->DrawIndexed(pGeometry->IndexCount, 0, 0);
    m_pContext->PSSetShader(m_pPixelShader, nullptr, 0);
}

void UiRenderer::ReleaseShader(Rml::CompiledShaderHandle aShader)
{
    delete reinterpret_cast<GradientConstants*>(aShader);
}
