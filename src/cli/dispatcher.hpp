#pragma once
#include <string>
#include <vector>

namespace icmg::cli {

class Dispatcher {
public:
    Dispatcher();
    int run(const std::vector<std::string>& args);

private:
    void printHelp() const;
    void printVersion() const;
};

} // namespace icmg::cli
