#pragma once

#include <filesystem>
#include "Stl.hpp"

namespace TiltedPhoques
{
    std::filesystem::path GetPath() noexcept;
    std::string LoadFile(const std::filesystem::path& acPath) noexcept;
    bool SaveFile(const std::filesystem::path& acPath, const std::string& acData) noexcept;
}
