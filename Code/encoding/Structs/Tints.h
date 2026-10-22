#pragma once

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Wire/Buffer.h>

using Wire::Buffer;

struct Tints
{
    struct Entry
    {
        std::string Name;
        float Alpha;
        uint32_t Color;
        uint32_t Type;

        bool operator==(const Entry& acRhs) const noexcept;
        bool operator!=(const Entry& acRhs) const noexcept;
    };

    Tints() = default;
    ~Tints() = default;

    bool operator==(const Tints& acRhs) const noexcept;
    bool operator!=(const Tints& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    std::vector<Entry> Entries;
};
