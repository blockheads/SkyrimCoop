#pragma once

#include <windows.h>
#include "AddressLibrary.h"
#include "RuntimeVersion.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <map>
#include <stdio.h>


class VersionDb
{
public:
    VersionDb() { Clear(); }
    ~VersionDb() {}

    static VersionDb& Get();

private:
    std::map<unsigned long long, unsigned long long> _data;
    std::map<unsigned long long, unsigned long long> _rdata;
    int _ver[4];
    std::string _verStr;
    std::string _moduleName;
    unsigned long long _base;

    static void* ToPointer(unsigned long long v) { return (void*)v; }

    static unsigned long long FromPointer(void* ptr) { return (unsigned long long)ptr; }

    static bool ParseVersionFromString(const char* ptr, int& major, int& minor, int& revision, int& build) { return sscanf_s(ptr, "%d.%d.%d.%d", &major, &minor, &revision, &build) == 4 && ((major != 1 && major != 0) || minor != 0 || revision != 0 || build != 0); }

public:
    const std::string& GetModuleName() const { return _moduleName; }
    const std::string& GetLoadedVersionString() const { return _verStr; }

    const std::map<unsigned long long, unsigned long long>& GetOffsetMap() const { return _data; }

    void* FindAddressById(unsigned long long id) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return NULL;

        unsigned long long offset = 0;
        if (!FindOffsetById(id, offset))
            return NULL;

        return ToPointer(b + offset);
    }

    bool FindOffsetById(unsigned long long id, unsigned long long& result) const
    {
        auto itr = _data.find(id);
        if (itr != _data.end())
        {
            result = itr->second;
            return true;
        }
        return false;
    }

    bool FindIdByAddress(void* ptr, unsigned long long& result) const
    {
        unsigned long long b = _base;
        if (b == 0)
            return false;

        unsigned long long addr = FromPointer(ptr);
        return FindIdByOffset(addr - b, result);
    }

    bool FindIdByOffset(unsigned long long offset, unsigned long long& result) const
    {
        auto itr = _rdata.find(offset);
        if (itr == _rdata.end())
            return false;

        result = itr->second;
        return true;
    }

    bool GetExecutableVersion(int& major, int& minor, int& revision, int& build) const
    {
        TCHAR szVersionFile[MAX_PATH];
        GetModuleFileName(NULL, szVersionFile, MAX_PATH);

        DWORD verHandle = 0;
        UINT size = 0;
        LPBYTE lpBuffer = NULL;
        DWORD verSize = GetFileVersionInfoSize(szVersionFile, &verHandle);

        if (verSize != NULL)
        {
            LPSTR verData = new char[verSize];

            if (GetFileVersionInfo(szVersionFile, verHandle, verSize, verData))
            {
                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\ProductVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }

                {
                    char* vstr = NULL;
                    UINT vlen = 0;
                    if (VerQueryValueA(verData, "\\StringFileInfo\\040904B0\\FileVersion", (LPVOID*)&vstr, &vlen) && vlen && vstr && *vstr)
                    {
                        if (ParseVersionFromString(vstr, major, minor, revision, build))
                        {
                            delete[] verData;
                            return true;
                        }
                    }
                }
            }

            delete[] verData;
        }

        return false;
    }

    void GetLoadedVersion(int& major, int& minor, int& revision, int& build) const
    {
        major = _ver[0];
        minor = _ver[1];
        revision = _ver[2];
        build = _ver[3];
    }

    void Clear()
    {
        _data.clear();
        _rdata.clear();
        for (int i = 0; i < 4; i++)
            _ver[i] = 0;
        _moduleName = std::string();
        _base = 0;
    }

    bool Load(const std::filesystem::path& acGamePath, const std::string& acExeVersion)
    {
        int major, minor, revision, build;

        if (!ParseVersionFromString(acExeVersion.c_str(), major, minor, revision, build))
        {
            return false;
        }

        return Load(acGamePath, major, minor, revision, build);
    }

    bool Load(const std::filesystem::path& acGamePath, int major, int minor, int revision, int build)
    {
        Clear();

        char fileName[256];
        _snprintf_s(fileName, 256, "versionlib-%d-%d-%d-%d.bin", major, minor, revision, build);

        std::ifstream file(acGamePath / "Data" / "SKSE" / "Plugins" / fileName, std::ios::binary);
        if (!file.good())
            return false;

        AddressLibrary::Data data;
        if (!AddressLibrary::Load(file, {major, minor, revision, build}, data))
            return false;
        _data = std::move(data.Offsets);
        _moduleName = std::move(data.Module);
        const int version[]{major, minor, revision, build};
        std::copy_n(version, 4, _ver);
        _verStr = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision) + "." + std::to_string(build);
        _base = reinterpret_cast<unsigned long long>(GetModuleHandleW(nullptr));
        for (const auto& [id, offset] : _data)
            _rdata.emplace(offset, id);

        return true;
    }

    bool DumpToTextFile(const std::string& path)
    {
        std::ofstream f = std::ofstream(path.c_str());
        if (!f.good())
            return false;

        for (auto itr = _data.begin(); itr != _data.end(); itr++)
        {
            f << std::dec;
            f << itr->first;
            f << '\t';
            f << std::hex;
            f << itr->second + 0x140000000;
            f << '\n';
        }

        return true;
    }
};

template <class T> struct VersionDbPtr
{
    VersionDbPtr(const uint32_t aId) noexcept
        : m_pPtr{nullptr}
        , m_id{aId}
    {
    }

    VersionDbPtr() = delete;
    VersionDbPtr(VersionDbPtr&) = delete;
    VersionDbPtr& operator=(VersionDbPtr&) = delete;

    operator T*() const noexcept { return Get(); }

    T* operator->() const noexcept { return Get(); }

    T* Get() const noexcept { return reinterpret_cast<T*>(GetPtr()); }

    void* GetPtr() const noexcept
    {
        if (m_pPtr == nullptr)
            m_pPtr = VersionDb::Get().FindAddressById(m_id);

        return m_pPtr;
    }

private:
    mutable void* m_pPtr;
    uint32_t m_id;
};

#define POINTER_SKYRIMSE(className, variableName, ...) static VersionDbPtr<className> variableName(__VA_ARGS__)
