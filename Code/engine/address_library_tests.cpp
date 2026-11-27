#include <catch2/catch.hpp>
#include <AddressLibrary.h>
#include <RuntimeVersion.h>
#include <sstream>
#include <algorithm>

namespace
{
template <class T> void Write(std::ostream& aOut, const T& acValue)
{
    aOut.write(reinterpret_cast<const char*>(&acValue), sizeof(acValue));
}

std::string Library(const std::array<int, 4>& acVersion)
{
    std::ostringstream out(std::ios::binary);
    Write(out, int32_t{5});
    Write(out, acVersion);
    std::array<char, 64> module{};
    std::copy_n("SkyrimSE.exe", 12, module.data());
    Write(out, module);
    Write(out, uint64_t{8});
    Write(out, uint32_t{5});
    for (uint32_t offset : {0u, 0x1000u, 0u, 0x2000u, 0x1000u}) Write(out, offset);
    return out.str();
}
}

TEST_CASE("Address libraries retain IDs across v5 zero holes", "[runtime][address-library]")
{
    AddressLibrary::Data data;
    const std::array<int, 4> newVersion{1, 7, 104, 0};
    std::istringstream latest(Library(newVersion), std::ios::binary);
    REQUIRE(AddressLibrary::Load(latest, newVersion, data));
    CHECK(data.Offsets.size() == 3);
    CHECK(data.Offsets.at(1) == 0x1000);
    CHECK(data.Offsets.at(3) == 0x2000);
    CHECK(data.Offsets.at(4) == 0x1000);
    CHECK_FALSE(data.Offsets.contains(0));
    CHECK_FALSE(data.Offsets.contains(2));
}

TEST_CASE("A wrong runtime or truncated library never publishes partial addresses", "[runtime][address-library]")
{
    AddressLibrary::Data data;
    const std::array<int, 4> version{1, 7, 104, 0};
    const auto bytes = Library(version);
    for (size_t length = 0; length < bytes.size(); ++length)
    {
        data.Offsets[42] = 0xDEAD;
        std::istringstream truncated(bytes.substr(0, length), std::ios::binary);
        REQUIRE_FALSE(AddressLibrary::Load(truncated, version, data));
        REQUIRE(data.Offsets.empty());
    }
    std::istringstream wrong(bytes, std::ios::binary);
    CHECK_FALSE(AddressLibrary::Load(wrong, {1, 6, 1170, 0}, data));
    CHECK(data.Offsets.empty());
}
