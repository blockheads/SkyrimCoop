#pragma once

#include <Games/Events.h>
#include <Interface/IMenu.h>

class MenuOpenCloseEvent;

struct IMapCameraCallbacks
{
    // add
    virtual void Unk_00(void); // 00
    virtual void Unk_01(void); // 01

    virtual ~IMapCameraCallbacks(); // 02
};
SKYRIM_STRUCT_ASSERT(sizeof(IMapCameraCallbacks) == 0x8);

struct MapMenu final : public IMenu, public BSTEventSink<MenuOpenCloseEvent>, public IMapCameraCallbacks
{
    uint8_t pad40[0x3058C - 0x40];
    bool controlsReady; // the map takes input once it has opened (MapMenu::PlaceMarker 53113 checks it)
};
SKYRIM_STRUCT_ASSERT(offsetof(MapMenu, controlsReady) == 0x3058C);
