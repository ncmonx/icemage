// 2026-06-06: auto-consolidate cooldown marker I/O. See auto_consolidate.hpp.
#include "auto_consolidate.hpp"
#include <fstream>

namespace icmg::imem {

long long readMarkerTs(const std::string& path) {
    std::ifstream f(path);
    if (!f) return 0;
    long long ts = 0;
    if (!(f >> ts)) return 0;   // corrupt / empty -> 0
    return ts < 0 ? 0 : ts;
}

void writeMarkerTs(const std::string& path, long long ts) {
    std::ofstream f(path, std::ios::trunc);
    if (f) f << ts;
}

} // namespace icmg::imem
