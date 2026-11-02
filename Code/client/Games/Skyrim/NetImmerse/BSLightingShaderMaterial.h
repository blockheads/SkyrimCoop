#pragma once

#include <cstddef>
#include <cstdint>

// CommonLibSSE-NG BSShaderMaterial / BSLightingShaderMaterialBase, SE/AE layout.
struct BSLightingShaderMaterial
{
    void* vtable;
    uint32_t references;
    float texCoordOffset[4];
    float texCoordScale[4];
    uint32_t hashKey;
    uint32_t unk30;
    uint32_t unk34;
    float specularColor[3];
    uint32_t pad44;
    void* diffuseTexture;
    int32_t diffuseRenderTargetSourceIndex;
    uint32_t pad54;
    void* normalTexture;
    void* rimSoftLightingTexture;
    void* specularBackLightingTexture;
    int32_t textureClampMode;
    uint32_t pad74;
    void* textureSet;
    float alpha;
    float refractionPower;
    float specularPower;
    float specularColorScale;
    float subSurfaceLightRolloff;
    float rimLightPower;
    uint32_t textureSetLock;
    uint32_t pad9C;
};

static_assert(offsetof(BSLightingShaderMaterial, texCoordScale) == 0x1C);
static_assert(offsetof(BSLightingShaderMaterial, diffuseTexture) == 0x48);
static_assert(offsetof(BSLightingShaderMaterial, normalTexture) == 0x58);
static_assert(sizeof(BSLightingShaderMaterial) == 0xA0);
