#pragma once

#include <memory>

// Owning a behavior graph reference from GetBSAnimationGraph, released on scope exit.
template <class T> struct ReleaseGraphReference
{
    void operator()(T* apGraph) const noexcept { apGraph->Release(); }
};

// Adopts the reference returned by GetBSAnimationGraph, without taking another one.
template <class T> using GraphReference = std::unique_ptr<T, ReleaseGraphReference<T>>;

template <class T, class Get, class Work> bool WithGraphReference(Get&& aGet, Work&& aWork)
{
    T* pGraph = nullptr;
    const bool cFound = aGet(&pGraph);
    GraphReference<T> graph(pGraph);
    return cFound && graph && aWork(graph.get());
}
