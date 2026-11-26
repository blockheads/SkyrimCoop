// JoinBridge's part for save sync (docs/design/join-flow.md, "Save sync"): the thread its heavy work runs on and the
// files of the saves a guest holds. No engine but the Saves folder's path.
#include <Bridges/JoinBridge.h>

#include <SaveLoadManager.h>
#include <base/threading/ThreadUtils.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <thread>

namespace
{
constexpr const char* kExtension = ".world";

std::string Hex(const std::string& acId) noexcept
{
    std::string hex;
    for (const unsigned char c : acId)
        hex += std::format("{:02x}", c);
    return hex;
}

std::optional<std::string> FromHex(const std::string& acHex) noexcept
{
    if (acHex.size() != 64)
        return std::nullopt;
    std::string id;
    for (size_t i = 0; i < acHex.size(); i += 2)
    {
        unsigned value = 0;
        if (std::from_chars(acHex.data() + i, acHex.data() + i + 2, value, 16).ptr != acHex.data() + i + 2)
            return std::nullopt;
        id += static_cast<char>(value);
    }
    return id;
}
} // namespace

struct JoinBridge::Worker
{
    explicit Worker(TaskQueue& aFinished) noexcept
        : Finished(aFinished)
        , Thread([this] { Run(); })
    {
    }

    ~Worker() noexcept
    {
        {
            std::scoped_lock lock(Lock);
            Stop = true;
        }
        Wake.notify_one();
        Thread.join();
    }

    void Post(std::function<std::function<void()>()> aWork) noexcept
    {
        {
            std::scoped_lock lock(Lock);
            Work.push_back(std::move(aWork));
        }
        Wake.notify_one();
    }

    void Run() noexcept
    {
        Base::SetCurrentThreadName("SkyrimCoop saves");
        for (;;)
        {
            std::unique_lock lock(Lock);
            Wake.wait(lock, [this] { return Stop || !Work.empty(); });
            if (Stop)
                return;
            const auto cWork = std::move(Work.front());
            Work.pop_front();
            lock.unlock();
            if (auto done = cWork())
                Finished.Add(std::move(done));
        }
    }

    TaskQueue& Finished;
    std::mutex Lock;
    std::condition_variable Wake;
    std::deque<std::function<std::function<void()>()>> Work;
    bool Stop{};
    std::thread Thread; // last: it runs at once
};

JoinBridge::JoinBridge() noexcept = default;
// Its work uses the bridge's members: joined first, while they live.
JoinBridge::~JoinBridge() noexcept
{
    m_pWorker.reset();
}

void JoinBridge::OffThread(std::function<std::function<void()>()> aWork) noexcept
{
    if (!m_pWorker)
        m_pWorker = std::make_unique<Worker>(m_finished);
    m_pWorker->Post(std::move(aWork));
}

void JoinBridge::FinishWork() noexcept
{
    m_finished.Drain();
}

const std::filesystem::path& JoinBridge::HeldFolder() noexcept
{
    std::call_once(m_heldFolderOnce, [this] {
        m_heldFolder = BGSSaveLoadManager::SaveFilePath("world").parent_path().parent_path() / "SkyrimCoop" / "worlds";
        std::error_code error;
        std::filesystem::create_directories(m_heldFolder, error);
    });
    return m_heldFolder;
}

std::vector<std::string> JoinBridge::HeldSaves() noexcept
{
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> found;
    std::error_code error;
    for (const auto& cEntry : std::filesystem::directory_iterator(HeldFolder(), error))
    {
        const auto& cPath = cEntry.path();
        const auto cId = cPath.extension() == kExtension ? FromHex(cPath.stem().string()) : std::nullopt;
        const auto cWritten = cEntry.last_write_time(error);
        if (cId && !error)
            found.emplace_back(cWritten, *cId);
    }
    std::ranges::sort(found, std::greater{});
    std::vector<std::string> ids;
    for (auto& [cWritten, id] : found)
        ids.push_back(std::move(id));
    return ids;
}

std::optional<std::string> JoinBridge::ReadHeldSave(const std::string& acId) noexcept
{
    std::ifstream file(HeldFolder() / (Hex(acId) + kExtension), std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0)
        return std::nullopt;
    std::string data(static_cast<size_t>(file.tellg()), '\0');
    file.seekg(0);
    return file.read(data.data(), static_cast<std::streamsize>(data.size())) ? std::optional(std::move(data)) : std::nullopt;
}

bool JoinBridge::WriteHeldSave(const std::string& acId, const std::string& acData) noexcept
{
    const auto cPath = HeldFolder() / (Hex(acId) + kExtension);
    std::error_code error;
    // There already (its name is its content's id): the newest again.
    if (std::filesystem::file_size(cPath, error) == acData.size() && !error)
    {
        std::filesystem::last_write_time(cPath, std::filesystem::file_time_type::clock::now(), error);
        return !error;
    }
    // Whole or not at all: a reader never sees half of it.
    auto partial = cPath;
    partial += ".part";
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        if (!file.write(acData.data(), static_cast<std::streamsize>(acData.size())))
            return false;
    }
    std::filesystem::rename(partial, cPath, error);
    return !error;
}

void JoinBridge::DropHeldSave(const std::string& acId) noexcept
{
    std::error_code error;
    std::filesystem::remove(HeldFolder() / (Hex(acId) + kExtension), error);
}
