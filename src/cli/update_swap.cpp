#include "update_swap.hpp"

#include <system_error>

namespace fs = std::filesystem;

namespace icmg::cli {

RollbackResult rollbackSwap(const fs::path& self, const fs::path& bak) {
    std::error_code ec;
    if (!fs::exists(bak, ec))
        return {false, "backup " + bak.string() + " is missing; kept the new binary"};

    fs::path swap = self;
    swap += ".swap";
    fs::remove(swap, ec);
    ec.clear();
    fs::rename(self, swap, ec);
    if (ec) return {false, "cannot move current binary aside: " + ec.message()};

    fs::rename(bak, self, ec);
    if (ec) {
        const std::string why = ec.message();
        std::error_code ec2;
        fs::rename(swap, self, ec2);  // put the current binary back
        if (ec2)
            return {false, "restore failed (" + why + ") and re-install failed (" +
                               ec2.message() + "); binary is at " + swap.string()};
        return {false, "restore failed: " + why + "; kept the new binary"};
    }
    fs::remove(swap, ec);
    return {true, {}};
}

void touchNow(const fs::path& p) {
    std::error_code ec;
    fs::last_write_time(p, fs::file_time_type::clock::now(), ec);
}

}  // namespace icmg::cli
