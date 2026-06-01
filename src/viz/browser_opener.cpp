#include "browser_opener.hpp"
#include <cstdlib>
#include <string>

#if defined(_WIN32)
  #include <windows.h>
  #include <shellapi.h>
#endif

namespace icmg::viz {

bool openInBrowser(const std::string& path) {
#if defined(_WIN32)
    // Use ShellExecuteA — returns > 32 on success
    HINSTANCE res = ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(res) > 32;
#elif defined(__APPLE__)
    std::string cmd = "open \"" + path + "\" 2>/dev/null &";
    return std::system(cmd.c_str()) == 0;
#else
    std::string cmd = "xdg-open \"" + path + "\" 2>/dev/null &";
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace icmg::viz
