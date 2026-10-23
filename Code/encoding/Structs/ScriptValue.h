#pragma once

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

#include <optional>
#include <string>
#include <type_traits>
#include <variant>

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
};

// A value as one game's script has it.
using LocalValue = ScriptValueOf<LocalForm>;

void SerializeScriptValue(Wire::Buffer::Writer& aWriter, const ScriptValue& acValue) noexcept;
// False: a kind this build does not know, so nothing after it can be read.
bool DeserializeScriptValue(Wire::Buffer::Reader& aReader, ScriptValue& aValue) noexcept;
