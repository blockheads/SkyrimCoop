#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace TestHarness
{
// A reply built and written off the game thread; a JSON error (a non-UTF-8 name) answers as a refusal. Pure: UnitTests.
inline std::string DumpReply(const std::function<nlohmann::json()>& acBuild)
{
    try
    {
        return acBuild().dump();
    }
    catch (const nlohmann::json::exception& acError)
    {
        return nlohmann::json{{"ok", false}, {"error", std::string("bad reply: ") + acError.what()}}.dump();
    }
}
} // namespace TestHarness
