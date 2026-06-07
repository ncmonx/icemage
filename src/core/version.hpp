// Central icmg version literal. Bump here + CMakeLists.txt + src/icmg.rc.
// All C++ code reads ICMG_VERSION; never embed version literals elsewhere.
#pragma once

namespace icmg::core {
inline constexpr const char* ICMG_VERSION = "2.0.11";
}  // namespace icmg::core