// v1.78.4: see update_lock.hpp
#include "update_lock.hpp"
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace icmg::core {

fs::path updatingLockPath() {
    const char* prof = std::getenv("USERPROFILE");
    if (!prof) prof = std::getenv("HOME");
    return fs::path(prof ? prof : ".") / ".icmg" / "updating.lock";
}

void writeUpdatingLock() {
    auto p = updatingLockPath();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p) << std::time(nullptr) << "\n";
}

void clearUpdatingLock() {
    std::error_code ec;
    fs::remove(updatingLockPath(), ec);
}

bool isUpdatingLockFresh() {
    auto p = updatingLockPath();
    std::error_code ec;
    if (!fs::exists(p, ec) || ec) return false;
    std::ifstream f(p);
    if (!f) return false;
    std::string line;
    if (!std::getline(f, line) || line.empty()) return false;
    try {
        std::time_t written = static_cast<std::time_t>(std::stoll(line));
        std::time_t age = std::time(nullptr) - written;
        return age >= 0 && age < 300; // fresh if < 5 minutes old
    } catch (...) {
        return false;
    }
}

} // namespace icmg::core
