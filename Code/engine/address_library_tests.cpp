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

std::string Library(int32_t aFormat, const std::array<int, 4>& acVersion)
{
    std::ostringstream out(std::ios::binary);
    Write(out, aFormat);
    Write(out, acVersion);
    if (aFormat == 5)
    {
        std::array<char, 64> module{};
        std::copy_n("SkyrimSE.exe", 12, module.data());
        Write(out, module);
        Write(out, uint64_t{8});
        Write(out, uint32_t{5});
        for (uint32_t offset : {0u, 0x1000u, 0u, 0x2000u, 0x1000u}) Write(out, offset);
    }
    else
    {
        Write(out, int32_t{12});
        out.write("SkyrimSE.exe", 12);
        Write(out, int32_t{8});
        Write(out, int32_t{3});
        Write(out, uint8_t{0}); Write(out, uint64_t{1}); Write(out, uint64_t{0x1000});
        Write(out, uint8_t{0x21}); Write(out, uint8_t{0x20});
        Write(out, uint8_t{0xB2}); Write(out, uint8_t{4}); Write(out, uint8_t{1});
    }
    return out.str();
}
}

TEST_CASE("Address libraries retain IDs across v2 deltas and v5 zero holes", "[runtime][address-library]")
{
    AddressLibrary::Data data;
    const std::array<int, 4> oldVersion{1, 6, 1170, 0}, newVersion{1, 7, 104, 0};
    std::istringstream old(Library(2, oldVersion), std::ios::binary);
    REQUIRE(AddressLibrary::Load(old, oldVersion, data));
    CHECK(data.Offsets.at(1) == 0x1000);
    CHECK(data.Offsets.at(2) == 0x1020);
    CHECK(data.Offsets.at(6) == 0x1018);
    std::istringstream latest(Library(5, newVersion), std::ios::binary);
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
    const auto bytes = Library(5, version);
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
