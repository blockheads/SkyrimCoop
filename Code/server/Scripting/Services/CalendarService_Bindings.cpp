#include "GameServer.h"

namespace Script
{
void CreateCalendarServiceBindings(sol::state_view aState)
{
    auto calendarType =
        aState.new_usertype<Server::CalendarService>("CalendarService", sol::meta_function::construct, sol::no_constructor);

    calendarType["get"] = []() -> Server::CalendarService& { return Server::GameServer::Get()->GetWorld().GetCalendarService(); };
    calendarType["GetTimeScale"] = []() { return Server::GameServer::Get()->GetWorld().GetCalendarService().GetTimeScale(); };
}
} // namespace Script
