#include <Interface/PartyMarkers.h>
#include <Interface/PartyMarkerLabel.h>

#include <Services/PartyMarkers.h>
#include <Interface/FxDelegateArgs.h>
#include <Interface/GFxMovie.h>
#include <Interface/IMenu.h>
#include <Interface/Menus/MapMenu.h>
#include <Interface/UI.h>
#include <Forms/BGSLocation.h>
#include <Forms/TESWorldSpace.h>
#include <NetImmerse/NiCamera.h>
#include <PlayerCharacter.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <mutex>
#include <algorithm>

namespace PartyMarkerDisplay
{
namespace
{
std::mutex s_lock;
PartyMarkerSet s_markers;

PartyMarkerSet Snapshot()
{
    const std::scoped_lock lock(s_lock);
    return s_markers;
}

GFxValue Number(double aValue)
{
    GFxValue value{};
    value.type = GFxValue::kNumber;
    value.value.number = aValue;
    return value;
}
GFxValue Boolean(bool aValue)
{
    GFxValue value{};
    value.type = GFxValue::kBoolean;
    value.value.boolean = aValue;
    return value;
}
GFxValue String(const char* acpText)
{
    GFxValue value{};
    value.type = GFxValue::kString;
    value.value.string = acpText;
    return value;
}
struct ScopedValue
{
    GFxValue Value{};
    ~ScopedValue()
    {
        if (Value.type & GFxValue::kManaged)
        {
            using TRelease = void(void*, GFxValue*, void*);
            POINTER_SKYRIMSE(TRelease, s_release, 82270);
            s_release.Get()(Value.objectInterface, &Value, Value.value.object);
        }
    }
};
std::optional<double> ReadNumber(GFxMovie* apMovie, const std::string& acPath)
{
    ScopedValue read;
    if (!apMovie->GetVariable(&read.Value, acPath.c_str())) return {};
    if ((read.Value.type & GFxValue::kTypeMask) == GFxValue::kNumber) return read.Value.value.number;
    if ((read.Value.type & GFxValue::kTypeMask) == GFxValue::kBoolean) return read.Value.value.boolean ? 1.0 : 0.0;
    return {};
}
void Set(GFxMovie* apMovie, const std::string& acPath, const GFxValue& acValue)
{
    apMovie->SetVariable(acPath.c_str(), acValue, 0);
}

// Compass::Update has already copied vanilla's markers; its next call lays out the complete array.
using TInvoke = bool(void*, void*, GFxValue*, const char*, const GFxValue*, uint64_t, bool);
TInvoke* RealCompassMarkers{};
bool CompassMarkers(void* apInterface, void* apData, GFxValue* apResult, const char* acpMethod,
                    const GFxValue* apArgs, uint64_t aCount, bool aDisplayObject)
{
    auto* pUI = UI::Get();
    auto* pMenu = pUI ? pUI->FindMenuByName("HUD Menu") : nullptr;
    GFxMovie* pMovie = pMenu ? pMenu->uiMovie : nullptr;
    constexpr const char* cpRoot = "_root.HUDMovieBaseInstance";
    constexpr const char* cpArray = "_root.HUDMovieBaseInstance.CompassTargetDataA";
    const auto cSet = Snapshot();
    std::vector<GFxValue> values;
    std::vector<const PartyMarker*> members;
    uint32_t start{};
    if (pMovie)
    {
        const auto cLength = ReadNumber(pMovie, std::string(cpArray) + ".length");
        struct Frames { uint32_t Quest, Door, Player, Enemy, Location, Undiscovered; };
        POINTER_SKYRIMSE(Frames, s_frames, 406118);
        if (cLength && *cLength >= 0 && *cLength <= 65536 && s_frames.Get()->Player)
        {
            start = static_cast<uint32_t>(*cLength);
            for (const auto& cMarker : cSet.Members)
            {
                const auto cBearing = PartyMarkers::Bearing(cSet.PlayerPosition, cMarker.Position);
                if (!PartyMarkers::SharesSpace(cSet, cMarker) || !cBearing) continue;
                members.push_back(&cMarker);
                values.insert(values.end(), {Number(*cBearing), Number(100), Number(s_frames.Get()->Player), Number(100)});
            }
            if (!values.empty() && !pMovie->SetVariableArray(5, cpArray, start, values.data(), uint32_t(values.size()), 0))
                members.clear();
        }
    }
    const bool cResult = RealCompassMarkers(apInterface, apData, apResult, acpMethod, apArgs, aCount, aDisplayObject);
    if (pMovie)
    {
        Set(pMovie, std::string(cpRoot) + ".SkyrimCoopPartyCount", Number(cResult ? members.size() : 0));
        Set(pMovie, std::string(cpRoot) + ".SkyrimCoopPartyStart", Number(start / 4));
        for (size_t i = 0; cResult && i < members.size(); ++i)
        {
            const std::string cPath = std::string(cpRoot) + ".CompassMarkerList." + std::to_string(start / 4 + i) + ".movie";
            Set(pMovie, cPath + ".partyName", String(members[i]->Name.c_str()));
        }
    }
    return cResult;
}

struct MapGroup
{
    std::vector<uint32_t> Players;
};
MapGroup s_worldGroup, s_localGroup;

// World-map coordinates include child-world map scale/offset (20496), as the native markers do.
std::optional<NiPoint3> MapPosition(const PartyMarkerSet& acSet, const PartyMarker& acMarker, TESWorldSpace* apMapWorld)
{
    auto* pWorld = Cast<TESWorldSpace>(TESForm::GetById(acMarker.WorldSpaceId));
    NiPoint3 position(acMarker.Position);
    if (!pWorld && acMarker.CellId == acSet.PlayerCellId)
    {
        const auto* pPlayer = PlayerCharacter::Get();
        const auto* pLocation = pPlayer ? Cast<BGSLocation>(pPlayer->locationForm) : nullptr;
        for (unsigned depth = 0; pLocation && depth < 32; ++depth, pLocation = pLocation->pParent)
        {
            auto* pMarker = TESObjectREFR::GetByHandle(pLocation->worldLocMarker);
            if (pMarker && (pWorld = pMarker->GetWorldSpace()))
            {
                position = pMarker->position;
                break;
            }
        }
    }
    bool matches = false;
    auto* pParent = pWorld;
    for (unsigned depth = 0; pParent && depth < 32; ++depth, pParent = pParent->parentWorld)
        if (pParent == apMapWorld) { matches = true; break; }
    if (!matches) return {};
    using TMapCoordinates = void(TESWorldSpace*, NiPoint3*, bool);
    POINTER_SKYRIMSE(TMapCoordinates, s_coordinates, 20496);
    s_coordinates.Get()(pWorld, &position, false);
    return position;
}

void DrawMap(GFxMovie* apMovie, const PartyMarkerSet& acSet, const char* acpMap, NiCamera* apCamera,
             TESWorldSpace* apWorld, bool aLocal, MapGroup& aGroup)
{
    const std::string cMap = acpMap;
    const std::string cParent = cMap + ".MarkerContainer";
    const std::string cGroup = cParent + ".SkyrimCoopPartyMarkers";
    std::vector<uint32_t> players;
    for (const auto& cMember : acSet.Members) players.push_back(cMember.PlayerId);
    const auto cPresent = ReadNumber(apMovie, cGroup + "._visible");
    if (players != aGroup.Players || !cPresent)
    {
        if (cPresent) apMovie->Invoke((cGroup + ".removeMovieClip").c_str(), nullptr, nullptr, 0);
        if (!players.empty())
        {
            ScopedValue depth;
            apMovie->Invoke((cParent + ".getNextHighestDepth").c_str(), &depth.Value, nullptr, 0);
            if ((depth.Value.type & GFxValue::kTypeMask) != GFxValue::kNumber) return;
            const GFxValue args[]{String("SkyrimCoopPartyMarkers"), Number(depth.Value.value.number + 1)};
            ScopedValue group;
            if (!apMovie->Invoke((cParent + ".createEmptyMovieClip").c_str(), &group.Value, args, 2)) return;
            for (size_t i = 0; i < acSet.Members.size(); ++i)
            {
                const auto& cMember = acSet.Members[i];
                const std::string cName = "Player" + std::to_string(cMember.PlayerId);
                const GFxValue markerArgs[]{String("PlayerSetMarker"), String(cName.c_str()), Number(i + 1)};
                ScopedValue marker;
                apMovie->Invoke((cGroup + ".attachMovie").c_str(), &marker.Value, markerArgs, 3);
                Set(apMovie, cGroup + "." + cName + ".enabled", Boolean(false));
                Set(apMovie, cGroup + "." + cName + ".hitTestDisable", Boolean(true));
            }
        }
        aGroup.Players = std::move(players);
    }
    const auto cWidth = ReadNumber(apMovie, cMap + ".MapWidth"), cHeight = ReadNumber(apMovie, cMap + ".MapHeight");
    for (const auto& cMember : acSet.Members)
    {
        const std::string cPath = cGroup + ".Player" + std::to_string(cMember.PlayerId);
        std::optional<NiPoint3> position;
        if (aLocal && PartyMarkers::SharesSpace(acSet, cMember))
            position = NiPoint3(cMember.Position);
        else if (!aLocal && apWorld)
            position = MapPosition(acSet, cMember, apWorld);
        NiPoint3 screen{};
        const bool cShown = apCamera && cWidth && cHeight && position &&
            apCamera->WorldPtToScreenPt3(*position, screen) && std::isfinite(screen.x) && std::isfinite(screen.y) &&
            screen.z >= 0 && screen.x >= 0 && screen.x <= 1 && screen.y >= 0 && screen.y <= 1;
        Set(apMovie, cPath + "._visible", Boolean(cShown));
        if (!cShown) continue;
        Set(apMovie, cPath + "._x", Number(screen.x * *cWidth));
        Set(apMovie, cPath + "._y", Number((1.f - screen.y) * *cHeight));
        SetPartyMarkerLabel(*apMovie, cPath, cMember.Name);
    }
}

using TMapUpdate = uint64_t(MapMenu*);
TMapUpdate* RealMapUpdate{};
uint64_t MapUpdate(MapMenu* apMenu)
{
    const uint64_t cResult = RealMapUpdate(apMenu);
    if (apMenu->uiMovie)
    {
        const auto cSet = Snapshot();
        auto* pCamera = apMenu->worldCamera.cameraNode ? apMenu->worldCamera.GetNiCamera() : nullptr;
        Set(apMenu->uiMovie, "_root.WorldMap.MarkerContainer.SkyrimCoopPartyMarkers._visible", Boolean(!apMenu->localMapShown));
        Set(apMenu->uiMovie, "_root.WorldMap.LocalMapMenu.IconDisplay.MarkerContainer.SkyrimCoopPartyMarkers._visible", Boolean(apMenu->localMapShown));
        if (apMenu->localMapShown)
            DrawMap(apMenu->uiMovie, cSet, "_root.WorldMap.LocalMapMenu.IconDisplay", apMenu->localMapCamera, nullptr, true, s_localGroup);
        else
            DrawMap(apMenu->uiMovie, cSet, "_root.WorldMap", pCamera, apMenu->worldSpace, false, s_worldGroup);
    }
    return cResult;
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    VersionDbPtr<uint8_t> update(51668), invoke(82256);
    uint8_t* pCall = update.Get() + 0x128;
    if (*pCall == 0xE8 && pCall + 5 + *reinterpret_cast<const int32_t*>(pCall + 1) == invoke.Get())
        Hooks::SwapCall(pCall, RealCompassMarkers, &CompassMarkers);
    else
        spdlog::error("Party markers: expected Compass::Update marker call missing");
    HOOK_ID(53111, &RealMapUpdate, MapUpdate);
});
} // namespace

void Set(const PartyMarkerSet& acMarkers) noexcept
{
    const std::scoped_lock lock(s_lock);
    s_markers = acMarkers;
}
} // namespace PartyMarkerDisplay
