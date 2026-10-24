#include <Games/ScriptAssignments.h>
#include <catch2/catch.hpp>

#include <array>

TEST_CASE("A compiled global assignment identifies just its target reference", "[Globals]")
{
    const std::array<uint8_t, 5> data{0, 'G', 0x34, 0x12, 0};
    CHECK(ScriptAssignments::GlobalTarget(data, 0x15, 1, false, false) == 0x1234);
    CHECK_FALSE(ScriptAssignments::GlobalTarget(data, 0x15, 1, true, false));
    CHECK_FALSE(ScriptAssignments::GlobalTarget(data, 0x15, 1, false, true));
    CHECK_FALSE(ScriptAssignments::GlobalTarget(data, 0x10, 1, false, false));
    CHECK_FALSE(ScriptAssignments::GlobalTarget(data, 0x15, 0, false, false));
    CHECK_FALSE(ScriptAssignments::GlobalTarget(data, 0x15, 3, false, false));
    CHECK_FALSE(ScriptAssignments::GlobalTarget(data, 0x15, UINT32_MAX, false, false));
    const std::array<uint8_t, 3> negative{'G', 0, 0x80};
    CHECK_FALSE(ScriptAssignments::GlobalTarget(negative, 0x15, 0, false, false));
    const std::array<uint8_t, 3> zero{'G', 0, 0};
    CHECK_FALSE(ScriptAssignments::GlobalTarget(zero, 0x15, 0, false, false));
}
