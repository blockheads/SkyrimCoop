#include <Forms/TESObjectCELL.h>
#include <TESObjectREFR.h>
#include <Forms/NavMesh.h>

Vector<TESObjectREFR*> TESObjectCELL::GetRefsByFormTypes(const Vector<FormType>& aFormTypes) const noexcept
{
    Vector<TESObjectREFR*> references{};

    if (!refData.refArray)
        return references;

    for (uint32_t i = 0; i < refData.capacity; i++)
    {
        TESObjectREFR* pRef = refData.refArray[i].Get();
        if (!pRef)
            continue;

        for (FormType formType : aFormTypes)
        {
            if (pRef->baseForm->formType == formType)
                references.push_back(pRef);
        }
    }

    return references;
}

void TESObjectCELL::GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept
{
    TP_THIS_FUNCTION(TGetCOCPlacementInfo, void, TESObjectCELL, NiPoint3*, NiPoint3*, bool);
    POINTER_SKYRIMSE(TGetCOCPlacementInfo, s_getCOCPlacementInfo, 19075);
    TiltedPhoques::ThisCall(s_getCOCPlacementInfo, this, aOutPos, aOutRot, aAllowCellLoad);
}

std::optional<float> TESObjectCELL::NavmeshFloorAt(const NiPoint3& acPoint, float aMaxGap) const noexcept
{
    if (!pNavMeshes)
        return std::nullopt;
    for (const NavMesh* pNavMesh : pNavMeshes->navMeshes)
    {
        if (!pNavMesh)
            continue;
        const auto& cVertices = pNavMesh->vertices;
        for (const BSNavmeshTriangle& cTriangle : pNavMesh->triangles)
        {
            if (cTriangle.vertices[0] >= cVertices.length || cTriangle.vertices[1] >= cVertices.length || cTriangle.vertices[2] >= cVertices.length)
                continue;
            const NiPoint3& a = cVertices[cTriangle.vertices[0]];
            const NiPoint3& b = cVertices[cTriangle.vertices[1]];
            const NiPoint3& c = cVertices[cTriangle.vertices[2]];
            // Barycentric weights of the point in the triangle seen from above.
            const float cDet = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
            if (std::abs(cDet) < 1e-3f)
                continue;
            const float u = ((b.y - c.y) * (acPoint.x - c.x) + (c.x - b.x) * (acPoint.y - c.y)) / cDet;
            const float v = ((c.y - a.y) * (acPoint.x - c.x) + (a.x - c.x) * (acPoint.y - c.y)) / cDet;
            const float w = 1.f - u - v;
            if (u < 0.f || v < 0.f || w < 0.f)
                continue;
            const float cFloor = u * a.z + v * b.z + w * c.z;
            if (std::abs(cFloor - acPoint.z) <= aMaxGap)
                return cFloor;
        }
    }
    return std::nullopt;
}
