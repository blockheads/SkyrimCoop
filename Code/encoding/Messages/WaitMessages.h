#pragma once

#include "Message.h"
#include <Messages/CellStateRequest.h>
#include <Messages/WorldState.h>
#include <Structs/TimeModel.h>

enum class WaitAction : uint8_t
{
    Start,
    Ready,
    Progress,
    Cancel,
    Failed
};
enum class WaitPhase : uint8_t
{
    Preparing,
    Running,
    Progress,
    Finished,
    Refused
};

// Only the host's game reports Progress; every player can cancel the current wait.
struct WaitRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kWaitRequest;
    WaitRequest() : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    WaitAction Action{WaitAction::Start};
    uint32_t Id{};
    uint32_t Hours{};            // Start: chosen hours
    uint32_t RemainingSeconds{}; // Progress: the native player's countdown, including partial hours
    bool Sleeping{};
};

// Preparing opens and pauses every game; Running follows only after every menu is ready.
struct NotifyWaitState final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyWaitState;
    NotifyWaitState() : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    WaitPhase Phase{WaitPhase::Preparing};
    uint32_t Id{};
    uint32_t Hours{};
    uint32_t RemainingSeconds{};
    bool Sleeping{}; // This game's player used the bed; everyone else waits.
    bool Driver{};   // Only the host's native menu advances on its timer.
    TimeModel Calendar;
};

// Only the completed shared rest asks for this refresh; ordinary cell loads use CellStateRequest.
struct WaitWorldRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kWaitWorldRequest;
    WaitWorldRequest() : RequestMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Id{};
    CellStateRequest State;
};

struct NotifyWaitWorld final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyWaitWorld;
    NotifyWaitWorld() : NotifyMessage(Opcode) {}

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Id{};
    bool Last{true}; // A cell may need several bounded messages.
    NotifyObjectChanges State;
};
