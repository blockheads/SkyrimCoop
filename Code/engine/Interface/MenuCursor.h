#pragma once

// The menus' mouse cursor (CommonLibSSE-NG MenuCursor, singleton 403551): where it points.
struct MenuCursor
{
    static MenuCursor* Get() noexcept;

    uint32_t unk00;
    float cursorPosX;
    float cursorPosY;
};
static_assert(offsetof(MenuCursor, cursorPosX) == 0x4);
static_assert(offsetof(MenuCursor, cursorPosY) == 0x8);
