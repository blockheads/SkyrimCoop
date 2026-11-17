#pragma once

#include <Services/SaveTransfer.h>

#include <optional>
#include <string>

class IJoinBridge;

// The saves a guest holds (the bridge's files, docs/design/join-flow.md, "Save sync"): the newest kKept, what the
// host encodes the next one against and what a host load of one of them needs no transfer for. Callable off thread.
namespace SaveStore
{
constexpr size_t kKept = 3;

// The image held under acId; one that no longer matches its id is dropped.
[[nodiscard]] std::optional<std::string> Read(IJoinBridge& aBridge, const std::string& acId) noexcept;
// The stream's image: decoded against the held save it names and checked against its id.
[[nodiscard]] std::optional<std::string> Decode(IJoinBridge& aBridge, const SaveTransfer::Stream& acStream) noexcept;
// Holds acImage under acId as the newest, and drops all but the newest kKept.
bool Keep(IJoinBridge& aBridge, const std::string& acId, const std::string& acImage) noexcept;
} // namespace SaveStore
