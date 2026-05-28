// v1.56 T1 Stage 3: cmake --build pattern profile.
//
// Collapses repetitive `[i/n] Building CXX object ...` lines into a single
// "(built N objects)" summary while preserving:
//   - error / warning / fatal lines (via isAlwaysVerbatim)
//   - the final Linking line (the outcome)
//   - any non-build line (e.g. cmake configure output)

#include "../pattern_pass.hpp"
#include "../dedup_pass.hpp"

#include <regex>
#include <sstream>
#include <string>

namespace icmg::tkil {

namespace {

bool matches(const std::string& cmd) {
    // cmake --build OR ninja-style direct build invocations.
    static const std::regex re(R"((^|\s)(cmake\s+--build|ninja|msbuild)\b)");
    return std::regex_search(cmd, re);
}

std::string apply(const std::string& in) {
    // Match e.g.: "[42/100] Building CXX object src/foo.cpp.o"
    static const std::regex build_re(R"(^\[\d+/\d+\]\s+Building .+$)");

    std::istringstream is(in);
    std::ostringstream os;
    std::string line;
    int build_run = 0;

    auto flush = [&]() {
        if (build_run >= 3) {
            os << "(built \xc3\x97" << build_run << " objects)\n";
        }
        build_run = 0;
    };

    while (std::getline(is, line)) {
        if (isAlwaysVerbatim(line)) {
            flush();
            os << line << '\n';
            continue;
        }
        if (std::regex_match(line, build_re)) {
            ++build_run;
            continue;
        }
        flush();
        os << line << '\n';
    }
    flush();
    return os.str();
}

}  // namespace

ICMG_REGISTER_PATTERN_PROFILE(cmake, "cmake-build", matches, apply)

}  // namespace icmg::tkil
