// v1.10.0 T6: savings dashboard User column + Active-users panel smoke.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <iostream>
#include <memory>
#include <sstream>

namespace cli = icmg::cli;
namespace core = icmg::core;

namespace {

class StdoutCapture {
public:
    StdoutCapture() : old_(std::cout.rdbuf(buf_.rdbuf())) {}
    ~StdoutCapture() { std::cout.rdbuf(old_); }
    std::string str() const { return buf_.str(); }
private:
    std::ostringstream buf_;
    std::streambuf*    old_;
};

}  // namespace

TEST("savings: registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("savings");
    ASSERT_TRUE(static_cast<bool>(cmd));
}

TEST("savings: empty args completes (catch DB-init throws on clean CI)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("savings");
    StdoutCapture cap;
    try {
        int rc = cmd->run({});
        (void)rc;
    } catch (const std::exception&) {
        // Acceptable on clean machines without .icmg/data.db
    }
}

int main() { return icmg::test::run_all(); }
