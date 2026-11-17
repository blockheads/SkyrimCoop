#pragma once

#include <NoCopyMove.h>
#include <Services/SaveTransfer.h>

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class IJoinBridge;

/**
 * @brief The host's side of save sync (docs/design/join-flow.md, "Save sync"): the images of the last saves it sent,
 * kept in memory to encode the next one against (its own files are overwritten: the quicksave slot, the autosaves),
 * and each image encoded once per base, off the game's threads. It outlives the host's rules, which a load remakes.
 */
struct SaveLibrary
{
    static constexpr size_t kKept = 3;

    explicit SaveLibrary(IJoinBridge& aBridge) noexcept;

    NO_COPY_MOVE(SaveLibrary);

    // Keeps the image (the newest kKept stay); its id.
    std::string Add(std::shared_ptr<const SaveTransfer::Image> apImage) noexcept;
    // acSave for a guest holding acHeld: against the newest of them kept here, else whole. None while it is being
    // encoded; no data when acSave is no longer kept.
    [[nodiscard]] std::shared_ptr<const SaveTransfer::Stream> Encode(const std::string& acSave, const std::vector<std::string>& acHeld) noexcept;
    void Clear() noexcept;

private:
    using Key = std::pair<std::string, std::string>; // the save, its base
    struct State
    {
        std::deque<std::shared_ptr<const SaveTransfer::Image>> Images; // the newest last
        std::map<Key, std::shared_ptr<const SaveTransfer::Stream>> Streams;
        std::set<Key> Encoding;
    };

    IJoinBridge& m_bridge;
    std::shared_ptr<State> m_pState = std::make_shared<State>();
};
