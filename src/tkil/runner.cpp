#include "runner.hpp"

namespace icmg::tkil {

// A1: parse argv from command string — respects quotes.
//
// Escaping rule: only \" (inside or outside quotes) is treated as an escaped
// double-quote character.  All other backslashes are literal — this is
// required so bare Windows paths like D:\Data Kerja\file are not mangled
// (the \D, \K sequences must NOT be consumed as escape sequences).
//
// To pass a path with spaces, wrap it in double quotes:
//   git add "D:\Data Kerja\my file.cs"
std::vector<std::string> parseArgv(const std::string& command) {
    std::vector<std::string> argv;
    std::string token;
    char quote = 0;

    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];

        // \" → literal " in all contexts (the only backslash escape we honour)
        if (c == '\\' && i + 1 < command.size() && command[i + 1] == '"') {
            token += '"';
            ++i;
            continue;
        }

        if (quote) {
            if (c == quote) { quote = 0; }
            else { token += c; }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ' ' || c == '\t') {
            if (!token.empty()) { argv.push_back(token); token.clear(); }
        } else {
            token += c;
        }
    }
    if (!token.empty()) argv.push_back(token);
    return argv;
}

} // namespace icmg::tkil
