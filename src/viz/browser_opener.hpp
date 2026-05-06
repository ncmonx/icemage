#pragma once
#include <string>

namespace icmg::viz {

// Open a local file in the default browser (cross-platform).
// path: absolute or relative file path.
// Returns true if the open command was launched (not if the browser loaded successfully).
bool openInBrowser(const std::string& path);

} // namespace icmg::viz
