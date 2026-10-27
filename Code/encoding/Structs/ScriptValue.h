#pragma once

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// A Papyrus value: None, a number, a bool, a string or a form (as TForm: this game's, or the session's).
template <class TForm> using ScriptValueOf = std::variant<std::monostate, int32_t, float, bool, std::string, TForm>;

// The value with its form made a TTo (aToForm: none when it cannot be), the rest as it is.
template <class TTo, class TFrom, class TToForm>
std::optional<ScriptValueOf<TTo>> MapForm(const ScriptValueOf<TFrom>& acValue, TToForm&& aToForm) noexcept
{
    return std::visit(
        [&aToForm]<class T>(const T& acAlternative) -> std::optional<ScriptValueOf<TTo>> {
            if constexpr (std::is_same_v<T, TFrom>)
            {
                if (auto form = aToForm(acAlternative))
                    return ScriptValueOf<TTo>{std::in_place_type<TTo>, *form};
                return std::nullopt;
            }
            else
                return ScriptValueOf<TTo>{std::in_place_type<T>, acAlternative};
        },
        acValue);
}

// An actor in the session (its session id), else a form from the game's data (its GameId).
struct ScriptForm
{
    bool operator==(const ScriptForm&) const noexcept = default;

    uint32_t SessionId{};
    GameId Form{};
};

// A value as the session carries it (Payloads::PlayerNative).
using ScriptValue = ScriptValueOf<ScriptForm>;

// A form of one game, by its id there.
struct LocalForm
{
    bool operator==(const LocalForm&) const noexcept = default;

    uint32_t FormId{};
    // "The player" of the story's party call: each game's own player, sent as the player's form (0x14), not as this
    // game's character (docs/design/dragonborn.md, the absorb).
    bool EachPlayer{};
};

// A value as one game's script has it.
using LocalValue = ScriptValueOf<LocalForm>;

// A script's call of a native, made on another game: its class and name, its object (a method's form; None for a
// global native) and its arguments, forms as TForm.
template <class TForm> struct ScriptCallOf
{
    bool operator==(const ScriptCallOf&) const noexcept = default;

    std::string Class{};
    std::string Function{};
    ScriptValueOf<TForm> Self{};
    std::vector<ScriptValueOf<TForm>> Arguments{};
};

// A call as the session carries it, and as one game's script makes it.
using ScriptCall = ScriptCallOf<ScriptForm>;
using LocalCall = ScriptCallOf<LocalForm>;

void SerializeScriptValue(Wire::Buffer::Writer& aWriter, const ScriptValue& acValue) noexcept;
// False: a kind this build does not know, so nothing after it can be read.
bool DeserializeScriptValue(Wire::Buffer::Reader& aReader, ScriptValue& aValue) noexcept;

void SerializeScriptCall(Wire::Buffer::Writer& aWriter, const ScriptCall& acCall) noexcept;
void DeserializeScriptCall(Wire::Buffer::Reader& aReader, ScriptCall& aCall) noexcept;
