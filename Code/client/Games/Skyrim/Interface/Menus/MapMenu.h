#pragma once

#include <Games/Events.h>
#include <Interface/IMenu.h>
#include <Camera/TESCamera.h>

struct MenuOpenCloseEvent;

struct IMapCameraCallbacks
{
    // add
    virtual void Unk_00(void); // 00
    virtual void Unk_01(void); // 01

    virtual ~IMapCameraCallbacks(); // 02
};
static_assert(sizeof(IMapCameraCallbacks) == 0x8);

struct MapMenu final : public IMenu, public BSTEventSink<MenuOpenCloseEvent>, public IMapCameraCallbacks
{
    uint8_t pad40[0x30358 - 0x40];
    NiCamera* localMapCamera; // LocalMapMenu::LocalMapCullingProcess::camera.camera
    uint8_t pad30360[0x3045C - 0x30360];
    bool localMapShown; // LocalMapMenu::showingMap (CommonLibSSE), in the embedded local map at 0x60
    bool localMapDragging;
    bool localMapControlsReady;
    uint8_t pad3045F[0x304A0 - 0x3045F];
    TESCamera worldCamera;
    uint8_t pad304D8[0x30538 - 0x304D8];
    struct TESWorldSpace* worldSpace;
    uint8_t pad30540[0x3058C - 0x30540];
    bool controlsReady; // the map takes input once it has opened (MapMenu::PlaceMarker 53113 checks it)
};
static_assert(offsetof(MapMenu, controlsReady) == 0x3058C);
static_assert(offsetof(MapMenu, localMapShown) == 0x3045C);
static_assert(offsetof(MapMenu, localMapControlsReady) == 0x3045E);

static_assert(offsetof(MapMenu, localMapCamera) == 0x30358);
static_assert(offsetof(MapMenu, worldCamera) == 0x304A0);
static_assert(offsetof(MapMenu, worldSpace) == 0x30538);
