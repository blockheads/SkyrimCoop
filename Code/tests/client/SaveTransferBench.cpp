#include <Services/SaveTransfer.h>
#include <chrono>
#include <fstream>
#include <iostream>

namespace
{
std::optional<std::string> Read(const char* acpPath)
{
    std::ifstream file(acpPath, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > SaveTransfer::kMaxSave)
        return std::nullopt;
    std::string bytes(static_cast<size_t>(file.tellg()), '\0');
    file.seekg(0);
    return file.read(bytes.data(), bytes.size()) ? std::optional(std::move(bytes)) : std::nullopt;
}

double Since(std::chrono::steady_clock::time_point aStart)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - aStart).count();
}
} // namespace

// What a guest holding OLD receives for NEW, and the time each step takes.
int main(int aCount, char** apArguments)
{
    if (aCount != 5)
    {
        std::cerr << "Usage: SaveTransferBench OLD.ess OLD.skse NEW.ess NEW.skse\n";
        return 2;
    }
    auto oldGame = Read(apArguments[1]), oldSKSE = Read(apArguments[2]);
    auto game = Read(apArguments[3]), skse = Read(apArguments[4]);
    if (!oldGame || !oldSKSE || !game || !skse)
        return 1;
    auto start = std::chrono::steady_clock::now();
    const auto cpOld = SaveTransfer::MakeImage({*oldGame, *oldSKSE});
    const auto cpNew = SaveTransfer::MakeImage({*game, *skse});
    if (!cpOld || !cpNew)
        return 1;
    const double cImageMs = Since(start) / 2;
    start = std::chrono::steady_clock::now();
    const auto cAlone = SaveTransfer::Encode(cpNew->Data);
    const double cAloneMs = Since(start);
    start = std::chrono::steady_clock::now();
    const auto cDelta = SaveTransfer::Encode(cpNew->Data, cpOld->Data);
    const double cDeltaMs = Since(start);
    start = std::chrono::steady_clock::now();
    const auto cDecoded = SaveTransfer::Decode(cDelta, cpOld->Data, static_cast<uint32_t>(cpNew->Data.size()));
    const double cDecodeMs = Since(start);
    if (!cDecoded || SaveTransfer::Digest(*cDecoded) != cpNew->Id || SaveTransfer::Decode(cAlone, {}, static_cast<uint32_t>(cpNew->Data.size())) != cpNew->Data)
        return 1;
    std::cout << "files=" << game->size() + skse->size() << " image=" << cpNew->Data.size() << " alone=" << cAlone.size()
              << " against_old=" << cDelta.size() << " image_ms=" << cImageMs << " alone_ms=" << cAloneMs
              << " against_old_ms=" << cDeltaMs << " decode_ms=" << cDecodeMs << "\n";
}
