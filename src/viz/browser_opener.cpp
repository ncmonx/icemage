#include "browser_opener.hpp"
#include <cstdlib>
#include <string>

#if defined(_WIN32)
  #include <windows.h>
  #include <shellapi.h>
#endif
#include "../core/win_api_utf8.hpp"

namespace icmg::viz {

bool openInBrowser(const std::string& path) {
#if defined(_WIN32)
    // ShellExecuteW via the UTF-8 wrapper (#285): non-ASCII paths open.
    return icmg::core::shellExecuteUtf8("open", path);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + path + "\" 2>/dev/null &";
    return std::system(cmd.c_str()) == 0;
#else
    std::string cmd = "xdg-open \"" + path + "\" 2>/dev/null &";
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace icmg::viz
