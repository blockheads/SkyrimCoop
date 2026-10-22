#pragma once

#include <cstdint>

struct AnimationVariables
{
    std::vector<bool> Booleans{};
    std::vector<uint32_t> Integers{};
    std::vector<float> Floats{};

    bool operator==(const AnimationVariables& acRhs) const noexcept;
    bool operator!=(const AnimationVariables& acRhs) const noexcept;

    void Load(std::istream&);
    void Save(std::ostream&) const;

    void GenerateDiff(const AnimationVariables& aPrevious, TiltedPhoques::Buffer::Writer& aWriter) const;
    void ApplyDiff(TiltedPhoques::Buffer::Reader& aReader);
    void VectorBool_to_String(const std::vector<bool>& bools, std::string& chars) const;
    void String_to_VectorBool(const std::string& chars, std::vector<bool>& bools);
};
