#pragma once
#include <cstdint>
#include <tsl/hopscotch_map.h>

class PointerTable {
public:
    void Insert(uint32_t formId, uint64_t ptr);
    void Remove(uint32_t formId);
    void RemoveByPointer(uint64_t ptr);
    uint64_t Lookup(uint32_t formId) const;  // returns 0 if not found
    bool Contains(uint32_t formId) const;
    size_t Size() const;
    void Clear();

    // Iterate all entries
    template<typename Fn>
    void ForEach(Fn&& fn) const {
        for (const auto& [formId, ptr] : m_map) {
            fn(formId, ptr);
        }
    }

private:
    tsl::hopscotch_map<uint32_t, uint64_t> m_map;
};
