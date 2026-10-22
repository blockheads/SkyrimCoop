#include "Buffer.h"

#include <algorithm>

namespace Wire
{
    namespace
    {
        // Whole bytes from the current bit to aCount bits further.
        size_t BytesSpanned(size_t aBitPosition, size_t aCount) noexcept
        {
            return ((aBitPosition & 0x7) + aCount + 7) / 8;
        }

        size_t NextWholeByte(size_t aBitPosition) noexcept
        {
            return (aBitPosition + 7) & ~size_t(0x7);
        }
    }

    bool Buffer::Reader::ReadBits(uint64_t& aDestination, size_t aCount)
    {
        aDestination = 0;

        const auto bitIndex = m_bitPosition & 0x7;
        auto bytesToRead = BytesSpanned(m_bitPosition, aCount);
        if (bytesToRead + GetBytePosition() > m_data.size())
            return false;

        const uint8_t* pLocation = m_data.data() + GetBytePosition();
        size_t bitsToRead = 0;
        uint64_t endBits = 0;

        // The rest of a partly read byte first.
        if (bitIndex != 0)
        {
            bitsToRead = std::min<size_t>(8 - bitIndex, aCount);
            endBits = ((*pLocation) >> bitIndex) & ((1 << bitsToRead) - 1);

            pLocation++;
            bytesToRead--;
        }

        std::copy(pLocation, pLocation + bytesToRead, reinterpret_cast<uint8_t*>(&aDestination));
        aDestination <<= bitsToRead;
        aDestination |= endBits;

        if (aCount < 64)
            aDestination &= (uint64_t(1) << aCount) - 1;

        m_bitPosition += aCount;
        return true;
    }

    bool Buffer::Reader::ReadBytes(std::span<uint8_t> aDestination)
    {
        m_bitPosition = NextWholeByte(m_bitPosition);
        if (aDestination.size() + GetBytePosition() > m_data.size())
            return false;

        std::copy_n(m_data.data() + GetBytePosition(), aDestination.size(), aDestination.data());
        m_bitPosition += aDestination.size() * 8;
        return true;
    }

    bool Buffer::Writer::Reserve(size_t aBytes)
    {
        const size_t cNeeded = GetBytePosition() + aBytes;
        if (!m_pBuffer)
            return cNeeded <= m_fixed.size();
        if (cNeeded > m_pBuffer->GetSize())
            m_pBuffer->Resize(m_pBuffer->GetSize() + std::max<size_t>(aBytes, 2048));
        return true;
    }

    bool Buffer::Writer::WriteBits(uint64_t aData, size_t aCount)
    {
        const auto bitIndex = m_bitPosition & 0x7;
        auto bytesToWrite = BytesSpanned(m_bitPosition, aCount);
        if (!Reserve(bytesToWrite))
            return false;

        uint8_t* pLocation = Data() + GetBytePosition();

        // Fill the rest of a partly written byte first, keeping its written bits.
        if (bitIndex != 0)
        {
            const auto bitsToWrite = std::min<size_t>(8 - bitIndex, aCount);
            const uint8_t cKept = *pLocation & ((1 << bitIndex) - 1);

            *pLocation = (static_cast<uint8_t>(aData & 0xFF) & ((1 << bitsToWrite) - 1)) << bitIndex;
            *pLocation |= cKept;

            pLocation++;
            bytesToWrite--;
            aData >>= bitsToWrite;
        }

        const auto* pBytes = reinterpret_cast<const uint8_t*>(&aData);
        std::copy(pBytes, pBytes + bytesToWrite, pLocation);

        m_bitPosition += aCount;
        return true;
    }

    bool Buffer::Writer::WriteBytes(std::span<const uint8_t> aSource)
    {
        m_bitPosition = NextWholeByte(m_bitPosition);
        if (!Reserve(aSource.size()))
            return false;

        std::copy(aSource.begin(), aSource.end(), Data() + GetBytePosition());
        m_bitPosition += aSource.size() * 8;
        return true;
    }
}
