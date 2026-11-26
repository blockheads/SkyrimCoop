#pragma once

#include <cstdint>
#include <functional>
#include <string>

// One game's part of a bug report (docs/design/bug-reports.md).
struct BugReportPart
{
    std::string Id;
    std::string Text;
    std::string By;          // who typed /bug
    uint64_t SessionTick{};  // the host's clock when it got it
    bool Host{};             // this game hosts: its part adds the save
    std::string Player;      // this game's player
};

// Where a game's part is written: the engine's state now, its log, messages, screen and (the host's) save.
class IBugReportBridge
{
  public:
    virtual ~IBugReportBridge() = default;
    // Takes what it needs now, writes it off the main thread; the folder it writes, as the player finds it.
    virtual std::string Write(const BugReportPart& acPart) noexcept = 0;
    // Everything since the last export in one zip to send, off the main thread; aDone gets the zip as the player
    // finds it (empty if it failed), on any thread.
    virtual void Export(const std::string& acPlayer, std::function<void(const std::string&)> aDone) noexcept = 0;
};
