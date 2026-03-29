#pragma once
#include <string>
#include <cstddef>

class LineBuffer
{
public:
    void Append(const char* aData, size_t aLen)
    {
        m_buf.append(aData, aLen);
    }

    // Extracts the first complete line (up to \n) into aOut, without the
    // trailing newline. Returns false if no complete line is available.
    bool PopLine(std::string& aOut)
    {
        auto pos = m_buf.find('\n');
        if (pos == std::string::npos)
            return false;

        aOut = m_buf.substr(0, pos);
        m_buf.erase(0, pos + 1);
        return true;
    }

    bool HasCompleteLine() const
    {
        return m_buf.find('\n') != std::string::npos;
    }

    void Clear()
    {
        m_buf.clear();
    }

private:
    std::string m_buf;
};
