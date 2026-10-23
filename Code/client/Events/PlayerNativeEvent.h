#pragma once

#include <Structs/ScriptValue.h>

#include <string>
#include <vector>

// On the host: a script run for a guest called a native on "the player", skipped here (PlayerNatives): that guest's
// game runs it instead (docs/design/party-model.md, rule 2).
struct PlayerNativeEvent
{
    uint32_t GuestFormId{};
    std::string Class{};
    std::string Function{};
    std::vector<LocalValue> Arguments{};
    uint32_t ReplyId{}; // one returning a value: the script waiting on it (PlayerNatives::Answer); 0: none
};
