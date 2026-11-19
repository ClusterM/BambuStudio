#pragma once

#include "Exception.h"

#include <bitset>

#ifdef _MSC_VER
  #include <intrin.h>
#endif

namespace OMC {

///  To ensure efficiency, we limit the maximum label count to less than
///   LABEL_NBIT (32).
constexpr int LABEL_NBIT = 32;

using Label = std::bitset<LABEL_NBIT>;

inline size_t LabelToIdx(const Label &b)
{
  OMC_EXPENSIVE_ASSERT(b.count() == 1, "more than 1 bit set to 1");
  uint32_t x = b.to_ulong();
#ifdef __GNUC__
  return __builtin_ffs(x) - 1; // GCC
#endif
#ifdef _MSC_VER
  // __lzcnt is an x86/x64 intrinsic. Exactly one bit is set, so the index of
  // the highest set bit is the label index on ARM too.
#  if defined(_M_ARM) || defined(_M_ARM64)
  // 31 - clz is the index of the single set bit.
  const auto clz = _CountLeadingZeros(static_cast<unsigned long>(x));
  return static_cast<size_t>(31 - clz);
#  else
  return 32 - __lzcnt(x) - 1; // Visual studio
#  endif
#endif
}
} // namespace OMC
