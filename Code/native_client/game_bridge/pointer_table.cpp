#include "pointer_table.h"
#include <spdlog/spdlog.h>

void PointerTable::Insert(uint32_t aFormId, uint64_t aPtr) {
    bool isNew = m_map.find(aFormId) == m_map.end();
    m_map[aFormId] = aPtr;
    if (isNew) {
        spdlog::info("PointerTable: registered formId=0x{:X} ptr=0x{:X} (total={})",
                     aFormId, aPtr, m_map.size());
    }
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
