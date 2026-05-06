#include "runner.hpp"

namespace icmg::rtk {

// A1: parse argv from command string — respects quotes
std::vector<std::string> parseArgv(const std::string& command) {
    std::vector<std::string> argv;
    std::string token;
    char quote = 0;

    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];

        if (quote) {
            if (c == quote) { quote = 0; }
            else { token += c; }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ' ' || c == '\t') {
            if (!token.empty()) { argv.push_back(token); token.clear(); }
        } else if (c == '\\' && i + 1 < command.size()) {
            token += command[++i];  // escaped char
        } else {
            token += c;
        }
    }
    if (!token.empty()) argv.push_back(token);
    return argv;
}

} // namespace icmg::rtk
