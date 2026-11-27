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
struct WaitRequest final : MessageOf<WaitRequest, kWaitRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    WaitAction Action{WaitAction::Start};
    uint32_t Id{};
    uint32_t Hours{};            // Start: chosen hours
    uint32_t RemainingSeconds{}; // Progress: the native player's countdown, including partial hours
    bool Sleeping{};
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::Bits<8>(Action), Id, Hours, RemainingSeconds, Sleeping);
    }
    bool operator==(const WaitRequest&) const noexcept = default;
};

// Preparing opens and pauses every game; Running follows only after every menu is ready.
struct NotifyWaitState final : MessageOf<NotifyWaitState, kNotifyWaitState>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    WaitPhase Phase{WaitPhase::Preparing};
    uint32_t Id{};
    uint32_t Hours{};
    uint32_t RemainingSeconds{};
    bool Sleeping{}; // The bed's player, and the host's party avatar for sleep events; other guests wait.
    bool Driver{};   // Only the host's native menu advances on its timer.
    TimeModel Calendar;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::Bits<8>(Phase), Id, Hours, RemainingSeconds, Sleeping, Driver, Calendar);
    }
    bool operator==(const NotifyWaitState&) const noexcept = default;
};

// Only the completed shared rest asks for this refresh; ordinary cell loads use CellStateRequest.
struct WaitWorldRequest final : MessageOf<WaitWorldRequest, kWaitWorldRequest>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Id{};
    CellStateRequest State;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, State); }
    bool operator==(const WaitWorldRequest&) const noexcept = default;
};

struct NotifyWaitWorld final : MessageOf<NotifyWaitWorld, kNotifyWaitWorld>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    uint32_t Id{};
    bool Last{true}; // A cell may need several bounded messages.
    NotifyObjectChanges State;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id, Last, State); }
    bool operator==(const NotifyWaitWorld&) const noexcept = default;
};
