#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

// On the host, what each guest's player sees: the engine answers a player's line of sight from its own camera (40520),
// so only the guest's game can say (docs/re/systems/papyrus-guest-scripts.md, "A player's line of sight"). The host
// keeps each guest's last answer per target and asks its game again, one question out at a time.
class HostSight
{
  public:
    using Clock = std::chrono::steady_clock;
    // Reply ids apart from the VM's stack ids, which PlayerNatives' other answers carry.
    static constexpr uint32_t kReplyBits = 0xC0000000;
    static constexpr auto kFresh = std::chrono::seconds(1); // an older answer is no answer
    static constexpr auto kLost = std::chrono::seconds(2);  // a question unanswered this long is asked again

    struct Question
    {
        uint32_t ReplyId;
        uint32_t Guest;  // the guest's character on the host
        uint32_t Target; // the reference it is asked about
    };
    struct Reading
    {
        std::optional<bool> Sees;    // none: no fresh answer yet
        std::optional<Question> Ask; // to send to the guest's game
    };

    // Whether aGuest's player sees aTarget, as its game last said, and the question to send when none is out.
    [[nodiscard]] Reading Sees(uint32_t aGuest, uint32_t aTarget, Clock::time_point aNow) noexcept;
    // The guest's game answered aReplyId. False if it is not one of this cache's questions.
    bool Answer(uint32_t aReplyId, bool aSees, Clock::time_point aNow) noexcept;

    [[nodiscard]] static constexpr bool IsSightReply(uint32_t aReplyId) noexcept
    {
        return (aReplyId & kReplyBits) == kReplyBits;
    }

    // A LOS registration's poll (54037) with no fresh answer: what to tell it so its state stays as it was, no event
    // sent. aLastState is the registration's (1 seen, 2 not, 0 never polled), written back as LastState.
    struct Poll
    {
        bool Sees;
        uint32_t LastState;
    };
    [[nodiscard]] static constexpr Poll Unknown(uint32_t aLastState) noexcept
    {
        return aLastState == 1 ? Poll{true, 1} : Poll{false, 2};
    }

  private:
    struct Entry
    {
        bool Sees = false;
        std::optional<Clock::time_point> AnsweredAt;
        std::optional<Clock::time_point> AskedAt; // a question is out
    };

    std::mutex m_lock; // a leaf: taken in the engine's line of sight
    std::map<std::pair<uint32_t, uint32_t>, Entry> m_entries;  // (guest, target)
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> m_asked; // reply id -> (guest, target)
    uint32_t m_next = 0;
};
