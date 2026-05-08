#include "user_identity.hpp"
#include "exec_utils.hpp"
#include <cstdlib>

namespace icmg::core {

const std::string& currentUser() {
    static std::string cached = []() {
        const char* env = std::getenv("ICMG_USER");
        if (env && *env) return std::string(env);
        // Fallback: git config user.email
        try {
            auto res = safeExecShell("git config user.email", false, 3000);
            if (res.exit_code == 0) {
                std::string s = res.out;
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
                if (!s.empty()) return s;
            }
        } catch (...) {}
        return std::string("anonymous");
    }();
    return cached;
}

} // namespace icmg::core
