// Include TiltedPhoques base before Message.h (needed when PCH is not active, e.g. ReplayTests target)
#include <glm/glm.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include <Recording/EnetRecorder.h>
#include <Recording/MessageSerializer.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <iomanip>
#include <sstream>

EnetRecorder::EnetRecorder(const std::string& aOutputDir)
    : m_outputDir(aOutputDir)
    , m_startTime(std::chrono::steady_clock::now())
{
    std::filesystem::create_directories(m_outputDir);
    spdlog::info("EnetRecorder: recording to {}", m_outputDir);
}

EnetRecorder::~EnetRecorder()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [peer, file] : m_peerFiles)
    {
        if (file.is_open())
            file.close();
    }
    spdlog::info("EnetRecorder: stopped recording ({} peer files)", m_peerFiles.size());
}

void EnetRecorder::RecordClientMessage(ConnectionId_t aPeer, const ClientMessage& acMessage)
{
    auto j = MessageSerializer::ClientToJson(acMessage);
    WriteLine(aPeer, j, "c2s");
}

void EnetRecorder::RecordServerMessage(ConnectionId_t aPeer, const ServerMessage& acMessage)
{
    auto j = MessageSerializer::ServerToJson(acMessage);
    WriteLine(aPeer, j, "s2c");
}

std::ofstream& EnetRecorder::GetPeerFile(ConnectionId_t aPeer)
{
    auto it = m_peerFiles.find(aPeer);
    if (it != m_peerFiles.end())
        return it->second;

    std::ostringstream filename;
    filename << m_outputDir << "/peer_" << std::hex << aPeer << ".ndjson";

    auto [inserted, ok] = m_peerFiles.emplace(aPeer, std::ofstream{});
    inserted->second.open(filename.str(), std::ios::app);

    if (!inserted->second.is_open())
        spdlog::error("EnetRecorder: failed to open {}", filename.str());
    else
        spdlog::info("EnetRecorder: opened {}", filename.str());

    return inserted->second;
}

void EnetRecorder::WriteLine(ConnectionId_t aPeer, nlohmann::json& aJson, const char* apDir)
{
    const auto cElapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - m_startTime).count();

    aJson["dir"] = apDir;
    aJson["peer"] = aPeer;
    aJson["ts"] = cElapsed;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto& file = GetPeerFile(aPeer);
    if (file.is_open())
    {
        file << aJson.dump() << "\n";
        file.flush();
    }
}
