#include <join/SaveTransfer.h>
#include "../../external/picosha2/picosha2.h"
#include <lz4.h>
#include <zlib.h>
#include <zstd.h>
#include <algorithm>
#include <bit>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#endif

namespace SaveTransfer
{
namespace
{
constexpr std::string_view cMagic = "TESV_SAVEGAME";
// The engine's save version with a compression type in its header (442580 refuses newer ones).
constexpr uint32_t cVersion = 12;

template <class T> T Read(std::string_view acData, size_t aOffset) noexcept
{
    T value{};
    std::memcpy(&value, acData.data() + aOffset, sizeof(T));
    return value;
}
} // namespace

std::string Pack(const SaveFiles& acFiles) noexcept
{
    if (acFiles.Game.size() > kMaxSave - 4 || acFiles.CoSave.size() > kMaxSave - 4 - acFiles.Game.size())
        return {};
    std::string packed(4, '\0');
    const auto cSize = static_cast<uint32_t>(acFiles.Game.size());
    std::memcpy(packed.data(), &cSize, sizeof(cSize));
    packed += acFiles.Game;
    packed += acFiles.CoSave;
    return packed;
}

std::optional<SaveFiles> Unpack(std::string_view acPacked) noexcept
{
    if (acPacked.size() < 4 || acPacked.size() > kMaxSave)
        return std::nullopt;
    uint32_t gameSize = 0;
    std::memcpy(&gameSize, acPacked.data(), sizeof(gameSize));
    if (gameSize > acPacked.size() - 4)
        return std::nullopt;
    return SaveFiles{std::string(acPacked.substr(4, gameSize)), std::string(acPacked.substr(4 + gameSize))};
}

std::string Digest(std::string_view acBytes) noexcept
{
    std::string digest(picosha2::k_digest_size, '\0');
#if defined(_WIN32)
    // CNG avoids the portable implementation's iterator overhead in development builds.
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                   reinterpret_cast<PUCHAR>(const_cast<char*>(acBytes.data())), static_cast<ULONG>(acBytes.size()),
                   reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size())) >= 0)
        return digest;
#endif
    picosha2::hash256(acBytes.begin(), acBytes.end(), digest.begin(), digest.end());
    return digest;
}

std::string Plain(std::string_view acGame) noexcept
{
    std::string asIs(acGame);
    // The magic, u32 header size, the header (version first; width, height and u16 compression type last), the
    // screenshot (4 bytes a pixel), then the body: raw, or u32 raw size, u32 packed size and the packed bytes.
    const size_t cHeader = cMagic.size() + 4;
    if (!acGame.starts_with(cMagic) || acGame.size() < cHeader)
        return asIs;
    const uint32_t cHeaderSize = Read<uint32_t>(acGame, cMagic.size());
    if (cHeaderSize < 14 || cHeaderSize > acGame.size() - cHeader || Read<uint32_t>(acGame, cHeader) != cVersion)
        return asIs;
    const size_t cHeaderEnd = cHeader + cHeaderSize;
    const uint64_t cShot = 4ull * Read<uint32_t>(acGame, cHeaderEnd - 10) * Read<uint32_t>(acGame, cHeaderEnd - 6);
    const uint16_t cType = Read<uint16_t>(acGame, cHeaderEnd - 2);
    if (cShot > acGame.size() - cHeaderEnd)
        return asIs;
    const size_t cBody = cHeaderEnd + static_cast<size_t>(cShot);

    std::string body;
    if (cType == 0)
        body = acGame.substr(cBody);
    else if ((cType == 1 || cType == 2) && acGame.size() - cBody >= 8)
    {
        const uint32_t cRaw = Read<uint32_t>(acGame, cBody);
        const uint32_t cPacked = Read<uint32_t>(acGame, cBody + 4);
        if (!cRaw || cRaw > kMaxSave || cPacked > acGame.size() - cBody - 8)
            return asIs;
        body.resize(cRaw);
        const char* cpPacked = acGame.data() + cBody + 8;
        if (cType == 2)
        {
            if (LZ4_decompress_safe(cpPacked, body.data(), static_cast<int>(cPacked), static_cast<int>(cRaw)) != static_cast<int>(cRaw))
                return asIs;
        }
        else
        {
            uLongf length = cRaw;
            if (uncompress(reinterpret_cast<Bytef*>(body.data()), &length, reinterpret_cast<const Bytef*>(cpPacked), cPacked) != Z_OK || length != cRaw)
                return asIs;
        }
    }
    else
        return asIs;

    std::string plain;
    plain.reserve(cBody + body.size());
    plain.append(acGame.substr(0, cHeaderEnd));
    plain[cHeaderEnd - 2] = plain[cHeaderEnd - 1] = '\0';
    plain.append(static_cast<size_t>(cShot), '\0');
    plain += body;
    return plain;
}

std::shared_ptr<const Image> MakeImage(const SaveFiles& acFiles) noexcept
{
    auto data = Pack({Plain(acFiles.Game), acFiles.CoSave});
    if (data.empty())
        return nullptr;
    auto pImage = std::make_shared<Image>();
    pImage->Id = Digest(data);
    pImage->Data = std::move(data);
    return pImage;
}

std::string Encode(std::string_view acTarget, std::string_view acBase) noexcept
{
    if (acTarget.empty() || acTarget.size() > kMaxSave || acBase.size() > kMaxSave)
        return {};
    ZSTD_CCtx* pContext = ZSTD_createCCtx();
    if (!pContext)
        return {};
    ZSTD_CCtx_setParameter(pContext, ZSTD_c_compressionLevel, 9);
    if (!acBase.empty())
    {
        // The window spans the base and the target, so a match can reach anywhere in the base.
        const int cWindow = std::clamp(static_cast<int>(std::bit_width(acBase.size() + acTarget.size())), 20, 30);
        ZSTD_CCtx_setParameter(pContext, ZSTD_c_windowLog, cWindow);
        ZSTD_CCtx_setParameter(pContext, ZSTD_c_enableLongDistanceMatching, 1);
        ZSTD_CCtx_refPrefix(pContext, acBase.data(), acBase.size());
    }
    std::string encoded(ZSTD_compressBound(acTarget.size()), '\0');
    const size_t cSize = ZSTD_compress2(pContext, encoded.data(), encoded.size(), acTarget.data(), acTarget.size());
    ZSTD_freeCCtx(pContext);
    if (ZSTD_isError(cSize))
        return {};
    encoded.resize(cSize);
    return encoded;
}

std::optional<std::string> Decode(std::string_view acEncoded, std::string_view acBase, uint32_t aSize) noexcept
{
    if (!aSize || aSize > kMaxSave || ZSTD_getFrameContentSize(acEncoded.data(), acEncoded.size()) != aSize)
        return std::nullopt;
    ZSTD_DCtx* pContext = ZSTD_createDCtx();
    if (!pContext)
        return std::nullopt;
    ZSTD_DCtx_setParameter(pContext, ZSTD_d_windowLogMax, 30);
    if (!acBase.empty())
        ZSTD_DCtx_refPrefix(pContext, acBase.data(), acBase.size());
    std::string decoded(aSize, '\0');
    const size_t cSize = ZSTD_decompressDCtx(pContext, decoded.data(), decoded.size(), acEncoded.data(), acEncoded.size());
    ZSTD_freeDCtx(pContext);
    if (ZSTD_isError(cSize) || cSize != aSize)
        return std::nullopt;
    return decoded;
}

bool Assembly::Add(const NotifySaveChunk& acChunk) noexcept
{
    if (acChunk.Save.size() != 32 || (!acChunk.Base.empty() && acChunk.Base.size() != 32) || !acChunk.Size || acChunk.Size > kMaxSave ||
        !acChunk.Total || acChunk.Total > kMaxSave || acChunk.Data.empty() || acChunk.Data.size() > NotifySaveChunk::kMaxData)
        return false;
    if (acChunk.Offset == 0)
    {
        m_stream = {acChunk.Save, acChunk.Base, acChunk.Size, {}};
        m_total = acChunk.Total;
    }
    if (acChunk.Save != m_stream.Save || acChunk.Base != m_stream.Base || acChunk.Size != m_stream.Size || acChunk.Total != m_total ||
        acChunk.Offset != m_stream.Data.size() || acChunk.Data.size() > m_total - m_stream.Data.size())
        return false;
    m_stream.Data += acChunk.Data;
    return true;
}
} // namespace SaveTransfer
