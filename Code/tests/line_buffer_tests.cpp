#include <catch2/catch.hpp>
#include "line_buffer.h"

#include <string>

TEST_CASE("Single complete line", "[LineBuffer]")
{
    LineBuffer buf;
    buf.Append("hello\n", 6);

    std::string line;
    REQUIRE(buf.PopLine(line));
    REQUIRE(line == "hello");
}

TEST_CASE("Two lines in one append", "[LineBuffer]")
{
    LineBuffer buf;
    buf.Append("a\nb\n", 4);

    std::string line;
    REQUIRE(buf.PopLine(line));
    REQUIRE(line == "a");
    REQUIRE(buf.PopLine(line));
    REQUIRE(line == "b");
}

TEST_CASE("Partial then complete", "[LineBuffer]")
{
    LineBuffer buf;
    buf.Append("hel", 3);

    std::string line;
    REQUIRE_FALSE(buf.PopLine(line));

    buf.Append("lo\n", 3);
    REQUIRE(buf.PopLine(line));
    REQUIRE(line == "hello");
}

TEST_CASE("No newline", "[LineBuffer]")
{
    LineBuffer buf;
    buf.Append("hello", 5);

    REQUIRE_FALSE(buf.HasCompleteLine());

    std::string line;
    REQUIRE_FALSE(buf.PopLine(line));
}

TEST_CASE("Empty line", "[LineBuffer]")
{
    LineBuffer buf;
    buf.Append("\n", 1);

    std::string line;
    REQUIRE(buf.PopLine(line));
    REQUIRE(line == "");
}

TEST_CASE("Leftover preserved", "[LineBuffer]")
{
    LineBuffer buf;
    buf.Append("a\nb", 3);

    std::string line;
    REQUIRE(buf.PopLine(line));
    REQUIRE(line == "a");
    REQUIRE_FALSE(buf.PopLine(line));

    buf.Append("\n", 1);
    REQUIRE(buf.PopLine(line));
    REQUIRE(line == "b");
}
