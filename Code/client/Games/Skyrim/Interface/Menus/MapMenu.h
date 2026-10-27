#pragma once

#include <Games/Events.h>
#include <Interface/IMenu.h>

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
    uint8_t pad40[0x3045C - 0x40];
    bool localMapShown; // LocalMapMenu::showingMap (CommonLibSSE), in the embedded local map at 0x60
    bool localMapDragging;
    bool localMapControlsReady;
    uint8_t pad3045F[0x3058C - 0x3045F];
    bool controlsReady; // the map takes input once it has opened (MapMenu::PlaceMarker 53113 checks it)
};
static_assert(offsetof(MapMenu, controlsReady) == 0x3058C);
static_assert(offsetof(MapMenu, localMapShown) == 0x3045C);
static_assert(offsetof(MapMenu, localMapControlsReady) == 0x3045E);
