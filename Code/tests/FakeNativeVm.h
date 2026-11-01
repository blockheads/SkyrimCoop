#pragma once

#include <cstddef>
#include <memory>

// Skyrim owns registered natives and deletes them through IFunction during VM teardown.
struct FakeNativeVm
{
    FakeNativeVm();
    ~FakeNativeVm();
    void ReleaseTypes();
    bool LaunchCheck() const;

    size_t Allocations = 0;
    size_t Destructions = 0;
    size_t GameHeapFrees = 0;

  private:
    struct Functions;
    std::unique_ptr<Functions> m_functions;
};
