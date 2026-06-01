// v1.41.x MSVC compat shim. MSYS2/MinGW expose POSIX names natively;
// MSVC strips the underscore-prefix from public API but doesn't alias
// the bare names. This header bridges the gap for cross-toolchain
// builds (Win MSVC + MSYS2 + Linux).
#pragma once

#ifdef _MSC_VER
#  include <cstdio>
#  include <cstdlib>
#  ifndef popen
#    define popen  _popen
#  endif
#  ifndef pclose
#    define pclose _pclose
#  endif
#endif
