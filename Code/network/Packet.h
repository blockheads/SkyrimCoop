#pragma once

#include <NoCopyMove.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Net
{
    // Above any real message (a player record's blobs go to 16 MB less a byte), below enet's 32 MB packet.
    inline constexpr size_t kMaxUncompressed = 16u << 20;

    // A compressed payload into aOut; false when broken or when its header claims more than kMaxUncompressed.
    [[nodiscard]] bool Uncompress(const void* apData, size_t aSize, std::string& aOut) noexcept;

    struct Packet
    {

        Packet() noexcept;
        Packet(uint32_t aSize) noexcept;
        virtual ~Packet() noexcept;

        NO_COPY_MOVE(Packet);

        // Get the start of the write buffer
        [[nodiscard]] char* GetData() const noexcept;

        // Get the writable data size, note that the actual packet size may differ from that
        [[nodiscard]] uint32_t GetSize() const noexcept;

        // Get the writable data size + protocol data size
        [[nodiscard]] uint32_t GetTotalSize() const noexcept;

        // Returns true if the packet has an associated buffer, usually used to check if the underlying allocator had enough space
        [[nodiscard]] bool IsValid() const noexcept;

    protected:

        friend struct Server;
        friend struct Client;

        char* m_pData;
        uint32_t m_size;
    };

    struct PacketView : Packet
    {
        PacketView(char* aPointer, uint32_t aSize);
        virtual ~PacketView();
    };
}
