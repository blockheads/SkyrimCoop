#pragma once

// The menus' mouse cursor (CommonLibSSE-NG MenuCursor, singleton 403551): where it points.
struct MenuCursor
{
    static MenuCursor* Get() noexcept;

    uint32_t unk00;
    float cursorPosX;
    float cursorPosY;
};
SKYRIM_STRUCT_ASSERT(offsetof(MenuCursor, cursorPosX) == 0x4);
SKYRIM_STRUCT_ASSERT(offsetof(MenuCursor, cursorPosY) == 0x8);
