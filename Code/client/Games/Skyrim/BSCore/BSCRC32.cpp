#include "BSCore/BSCRC32.h"

namespace creation
{
// Standard CRC-32 (IEEE 802.3, reflected 0xEDB88320; same as CryptoPP::CRC32 and zlib)
// over the 4 little-endian bytes of auiData. Replaces CryptoPP, which refuses to
// build with clang-cl.
void BSCRC32::GenerateCRC(uint32_t& aiCRC, uint32_t auiData, uint32_t auiDefaultvalue)
{
#if defined(DEBUG)
    // Matches the previous implementation, which could not apply a default value.
    if (auiDefaultvalue != 0)
        __debugbreak();
#endif

    uint32_t crc = 0xFFFFFFFFu;
    for (int byte = 0; byte < 4; ++byte)
    {
        crc ^= (auiData >> (8 * byte)) & 0xFFu;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    aiCRC = ~crc;
}
} // namespace creation
