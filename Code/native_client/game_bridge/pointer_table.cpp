#include "pointer_table.h"

void PointerTable::Insert(uint32_t aFormId, uint64_t aPtr) {
    m_map[aFormId] = aPtr;
}

void PointerTable::Remove(uint32_t aFormId) {
    m_map.erase(aFormId);
}

void PointerTable::RemoveByPointer(uint64_t aPtr) {
    for (auto it = m_map.begin(); it != m_map.end();) {
        if (it->second == aPtr) {
            it = m_map.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t PointerTable::Lookup(uint32_t aFormId) const {
    auto it = m_map.find(aFormId);
    if (it != m_map.end())
        return it->second;
    return 0;
}

bool PointerTable::Contains(uint32_t aFormId) const {
    return m_map.count(aFormId) > 0;
}

size_t PointerTable::Size() const {
    return m_map.size();
}

void PointerTable::Clear() {
    m_map.clear();
}
