#include <catch2/catch.hpp>

#include <Games/Animation/ReplayCapture.h>

TEST_CASE("Resolved animation queries are never captured as performed actions", "[animation][replay-capture]")
{
    uint32_t flags = GENERATE(1u, 3u);
    uint32_t calls = 0;
    const auto cOutcome = ReplayCapture::Run(flags, [&]() -> uint8_t {
        ++calls;
        flags = 2;
        return 1;
    });
    CHECK(calls == 1);
    CHECK(cOutcome.Result == 1);
    CHECK_FALSE(cOutcome.Capture);
}

TEST_CASE("Animation capture preserves performed actions and the native result", "[animation][replay-capture]")
{
    uint32_t flags = GENERATE(0u, 2u);
    const uint8_t cResult = GENERATE(uint8_t{0}, uint8_t{1});
    const auto cOutcome = ReplayCapture::Run(flags, [&] { return cResult; });
    CHECK(cOutcome.Result == cResult);
    CHECK(cOutcome.Capture);
}

TEST_CASE("A native action left as a query is not captured", "[animation][replay-capture]")
{
    uint32_t flags = 0;
    const auto cOutcome = ReplayCapture::Run(flags, [&]() -> uint8_t {
        flags = 1;
        return 1;
    });
    CHECK(cOutcome.Result == 1);
    CHECK_FALSE(cOutcome.Capture);
}
