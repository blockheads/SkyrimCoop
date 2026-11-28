#pragma once

#include <Interface/FxDelegateArgs.h>

#include <cstdint>
#include <optional>

// A Scaleform array's elements through the engine's GFxValue::ObjectInterface (CommonLibSSE-NG; IDs in the .cpp).
namespace GFxArray
{
[[nodiscard]] uint32_t Size(const GFxValue& acArray) noexcept;
// Element aIndex's number member acpName; none when it has none.
[[nodiscard]] std::optional<double> NumberOf(const GFxValue& acArray, uint32_t aIndex, const char* acpName) noexcept;
void Remove(const GFxValue& acArray, uint32_t aIndex) noexcept;
// A value read from Scaleform let go (ObjectRelease for a managed one), and cleared.
void Release(GFxValue& aValue) noexcept;
} // namespace GFxArray
