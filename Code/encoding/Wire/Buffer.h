#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Wire
{
    // Bytes a message is written into; a Writer grows it.
    struct Buffer
    {
        Buffer() noexcept = default;
        explicit Buffer(size_t aSize) : m_data(aSize) {}
        explicit Buffer(std::span<const uint8_t> aData) : m_data(aData.begin(), aData.end()) {}

        uint8_t operator[](size_t aIndex) const noexcept { return m_data[aIndex]; }
        uint8_t& operator[](size_t aIndex) noexcept { return m_data[aIndex]; }

        [[nodiscard]] size_t GetSize() const noexcept { return m_data.size(); }
        void Resize(size_t aSize) { m_data.resize(aSize); }

        [[nodiscard]] const uint8_t* GetData() const noexcept { return m_data.data(); }
        [[nodiscard]] uint8_t* GetWriteData() noexcept { return m_data.data(); }

        // Reads bits and whole bytes from bytes it does not own.
        struct Reader
        {
            explicit Reader(std::span<const uint8_t> aData) noexcept : m_data(aData) {}
            explicit Reader(const Buffer* apBuffer) noexcept : m_data(apBuffer->GetData(), apBuffer->GetSize()) {}

            bool ReadBits(uint64_t& aDestination, size_t aCount);
            bool ReadBytes(std::span<uint8_t> aDestination);

            [[nodiscard]] bool Eof() const noexcept { return m_bitPosition >= m_data.size() * 8; }
            // Bytes read so far, the last partly read one included.
            [[nodiscard]] size_t Size() const noexcept { return (m_bitPosition + 7) / 8; }
            [[nodiscard]] size_t GetBytePosition() const noexcept { return m_bitPosition / 8; }
            [[nodiscard]] size_t GetBitPosition() const noexcept { return m_bitPosition; }

        private:
            std::span<const uint8_t> m_data;
            size_t m_bitPosition = 0;
        };

        // Writes bits and whole bytes: into a Buffer, growing it, or into fixed bytes, failing past their end.
        struct Writer
        {
            explicit Writer(Buffer* apBuffer) noexcept : m_pBuffer(apBuffer) {}
            explicit Writer(std::span<uint8_t> aData) noexcept : m_fixed(aData) {}

            bool WriteBits(uint64_t aData, size_t aCount);
            bool WriteBytes(std::span<const uint8_t> aSource);

            // Bytes written so far, the last partly written one included.
            [[nodiscard]] size_t Size() const noexcept { return (m_bitPosition + 7) / 8; }
            [[nodiscard]] size_t GetBytePosition() const noexcept { return m_bitPosition / 8; }
            [[nodiscard]] size_t GetBitPosition() const noexcept { return m_bitPosition; }

        private:
            // Room for aBytes more from the current byte.
            bool Reserve(size_t aBytes);
            uint8_t* Data() noexcept { return m_pBuffer ? m_pBuffer->GetWriteData() : m_fixed.data(); }

            Buffer* m_pBuffer = nullptr;
            std::span<uint8_t> m_fixed;
            size_t m_bitPosition = 0;
        };

    private:
        std::vector<uint8_t> m_data;
    };
}
