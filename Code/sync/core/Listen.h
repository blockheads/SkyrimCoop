#pragma once

#include <entt/signal/dispatcher.hpp>
#include <vector>

template <auto Handler, class Sink, class T>
void Listen(std::vector<entt::scoped_connection>& aConnections, Sink aSink, T* apInstance)
{
    aConnections.emplace_back(aSink.template connect<Handler>(apInstance));
}

template <class Event, auto Handler, class T>
void Listen(std::vector<entt::scoped_connection>& aConnections, entt::dispatcher& aDispatcher, T* apInstance)
{
    Listen<Handler>(aConnections, aDispatcher.sink<Event>(), apInstance);
}
