#include "Packet.h"
#include "ENetInterface.h"

#include <snappy.h>

namespace Net
{
    bool Uncompress(const void* apData, size_t aSize, std::string& aOut) noexcept
    {
        size_t size = 0;
        const auto* cpData = static_cast<const char*>(apData);
        if (!snappy::GetUncompressedLength(cpData, aSize, &size) || size > kMaxUncompressed)
            return false;
        return snappy::Uncompress(cpData, aSize, &aOut);
    }

    Packet::Packet() noexcept
        : m_pData(nullptr)
        , m_size(0)
    {
    }

    Packet::Packet(const uint32_t aSize) noexcept
        : m_pData(nullptr)
        , m_size(aSize + 1)
    {
        m_pData = new char[m_size];
        m_pData[0] = kPayload;
    }

    Packet::~Packet() noexcept
    {
        delete[] m_pData;
    }

    char* Packet::GetData() const noexcept
    {
        return m_pData + 1;
    }

    uint32_t Packet::GetSize() const noexcept
    {
        return m_size - 1;
    }

    uint32_t Packet::GetTotalSize() const noexcept
    {
        return m_size;
    }

    bool Packet::IsValid() const noexcept
    {
        return m_pData != nullptr;
    }

    PacketView::PacketView(char* aPointer, uint32_t aSize)
        : Packet()
    {
        m_pData = aPointer;
        m_size = aSize;

        m_pData[0] = kPayload;
    }

    PacketView::~PacketView()
    {
        m_pData = nullptr;
        m_size = 0;
    }
}
