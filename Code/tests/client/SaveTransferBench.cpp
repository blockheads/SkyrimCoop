#include <Services/SaveTransfer.h>
#include <snappy.h>
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
}

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
    const auto cStart = std::chrono::steady_clock::now();
    SaveTransfer::Cache cache;
    cache.Load({*oldGame, *oldSKSE});
    const auto cPacked = SaveTransfer::Pack({*game, *skse});
    auto chunks = SaveTransfer::Split(cPacked);
    uint64_t cold = 0, warm = 0, reused = 0, hits = 0;
    SaveTransfer::Assembly assembly;
    for (auto& chunk : chunks)
    {
        cold += chunk.Data.size();
        if (cache.Find(chunk.Hash))
        {
            reused += chunk.RawSize;
            ++hits;
            chunk.Data.clear();
        }
        warm += chunk.Data.size();
        if (!assembly.Add(chunk, &cache))
            return 1;
    }
    if (!assembly.Complete() || assembly.Data() != cPacked)
        return 1;
    const double cMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cStart).count();
    std::string whole;
    snappy::Compress(cPacked.data(), cPacked.size(), &whole);
    std::cout << "raw_bytes=" << cPacked.size() << " legacy_whole_compressed=" << whole.size()
              << " cold_payload=" << cold << " warm_payload=" << warm << " reused_raw=" << reused
              << " cached_blocks=" << hits << "/" << chunks.size() << " cpu_ms=" << cMs
              << " hash_advertisement_bytes=" << cache.Hashes().size()*33 << "\n";
}
