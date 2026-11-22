#pragma once

#include "Message.h"

#include <cstdint>

// What the host's NPCs see of a guest's player, to that guest only on change (stealth.md "The eye is the host's"):
// its copy's highest detection level and seeing count (41408), lowest stealth points (46877).
struct NotifyStealthEye final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyStealthEye;
    static constexpr int32_t kUnseen = -1000; // 41408 with nobody judging it, the floor sent

    NotifyStealthEye()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyStealthEye& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Level == acRhs.Level && Seeing == acRhs.Seeing && Stealth == acRhs.Stealth;
    }

    int32_t Level{kUnseen};
    uint32_t Seeing{};
    float Stealth{100.f};
};
