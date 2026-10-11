#pragma once

#include "Message.h"

struct NotifyHealthChangeBroadcast final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyHealthChangeBroadcast;

    NotifyHealthChangeBroadcast()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyHealthChangeBroadcast& acRhs) const noexcept { return Id == acRhs.Id && DeltaHealth == acRhs.DeltaHealth && HasAttacker == acRhs.HasAttacker && AttackerId == acRhs.AttackerId && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t Id;
    float DeltaHealth;
    // Server id of the attacker, when known (owners use it for the victim's reaction and kills).
    bool HasAttacker{false};
    uint32_t AttackerId{0};
};
