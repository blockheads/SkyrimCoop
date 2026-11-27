#include <MenuEvents.h>

#include <Events/EventDispatcher.h>
#include <core/MenuEvent.h>
#include <Events.h>
#include <Interface/MenuOpenCloseEvent.h>
#include <Interface/UI.h>
#include <core/World.h>

namespace MenuEvents
{
namespace
{
struct MenuSink final : BSTEventSink<MenuOpenCloseEvent>
{
    BSTEventResult OnEvent(const MenuOpenCloseEvent* apEvent, const EventDispatcher<MenuOpenCloseEvent>*) override
    {
        const char* cpName = apEvent ? apEvent->menuName.AsAscii() : nullptr;
        if (cpName)
            World::Runner().Trigger(MenuEvent{cpName, apEvent->opening});
        return BSTEventResult::kOk;
    }
};

MenuSink s_menus;
bool s_registered = false;
} // namespace

void Start() noexcept
{
    UI* pUI = UI::Get();
    if (!pUI || s_registered)
        return;
    pUI->MenuOpenCloseSource()->RegisterSink(&s_menus);
    s_registered = true;
}

// Before a hot reload unloads this code.
void Stop() noexcept
{
    UI* pUI = UI::Get();
    if (!pUI || !s_registered)
        return;
    pUI->MenuOpenCloseSource()->UnRegisterSink(&s_menus);
    s_registered = false;
}
} // namespace MenuEvents
