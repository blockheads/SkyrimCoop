#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include "AnimationVariables.h"
#include <Wire/Buffer.h>

struct ActionEvent
{
    uint64_t Tick{0};
    uint32_t ActorId{0};
    uint32_t ActionId{0};
    uint32_t TargetId{0};
    // An actor target between games: its session id + 1 (0 none), as its form id names nothing in the other game.
    uint32_t TargetActor{0};
    uint32_t IdleId{0};
    uint32_t State1{0};
    uint32_t State2{0};
    uint32_t Type{0};
    std::string EventName{};
    std::string TargetEventName{};
    AnimationVariables Variables{};

    ActionEvent() = default;
    ActionEvent(const ActionEvent& acRhs) = default;
    ActionEvent(ActionEvent&& acRhs) = default;
    ~ActionEvent() = default;
    ActionEvent& operator=(const ActionEvent& acRhs) = default;
    ActionEvent& operator=(ActionEvent&& acRhs) = default;

    bool operator==(const ActionEvent& acRhs) const noexcept;
    bool operator!=(const ActionEvent& acRhs) const noexcept;

    void Load(std::istream&);
    void Save(std::ostream&) const;

    void GenerateDifferential(const ActionEvent& aPrevious, Wire::Buffer::Writer& aWriter) const noexcept;
    void ApplyDifferential(Wire::Buffer::Reader& aReader) noexcept;
};
