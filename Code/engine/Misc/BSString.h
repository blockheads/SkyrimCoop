#pragma once

struct BSString
{
    BSString()
        : data{nullptr}
        , length{0}
        , capacity{0}
    {
    }
    ~BSString();

    [[nodiscard]] const char* AsAscii() const { return data; }
    // BSString::Set (439876): a copy of acpText (aLength 0: up to its end); false when empty.
    bool Set(const char* acpText, uint32_t aLength = 0) noexcept;

private:
    const char* data;
    uint16_t length;
    uint16_t capacity;
};
