#pragma once

#include "MovementApi.h"
#include <filesystem>
#include <mutex>
#include <string>

namespace CoopReload
{
// Resident code: never put this object, its mutex, or its callers in the module.
// The lock covers the complete call as well as swapping/unloading. A caller can
// therefore never execute code from a DLL after that DLL has been unloaded.
class MovementLoader
{
public:
    ~MovementLoader();
    bool Reload(const std::filesystem::path& path, std::string& error);
    void Unload();
    bool Evaluate(const CoopMovementInput& input, CoopMovementOutput& output);
    std::string BuildId() const;
    uint64_t Generation() const;
    uint64_t Calls() const;
    uint64_t InvalidOutputs() const;

private:
    mutable std::mutex m_mutex;
    void* m_library = nullptr;
    CoopMovementApi m_api{};
    std::filesystem::path m_path;
    uint64_t m_generation = 0;
    uint64_t m_calls = 0;
    uint64_t m_invalidOutputs = 0;
};
}
