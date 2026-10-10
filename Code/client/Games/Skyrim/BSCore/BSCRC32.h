#pragma once

namespace creation
{
struct BSCRC32
{
    static void GenerateCRC(uint32_t& aiCRC, uint32_t auiData, uint32_t auiDefaultvalue);
};
} // namespace creation
