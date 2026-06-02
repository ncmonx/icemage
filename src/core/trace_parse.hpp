#pragma once
// v2.0.0 TE3: runtime/dynamic dependency extraction. Parse stack traces / runtime
// logs into ordered frames, then derive adjacency edges (caller<->callee as they
// appear) for a `runtime_call` edge type distinct from static `calls`. Pure +
// header-only so it is unit-testable with no graph/DB.
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::core {

struct StackFrame {
    std::string file;
    std::string func;
    int         line = 0;
};

struct RuntimeEdge {
    std::string from_file, from_func;
    std::string to_file,   to_func;
};

// Parse python / node-js / gdb-c++ frames from arbitrary text, in source order.
inline std::vector<StackFrame> parseStackFrames(const std::string& text) {
    std::vector<StackFrame> out;
    // Python:  File "app.py", line 10, in main
    static const std::regex rePy(R"rx(File "([^"]+)", line (\d+), in (\S+))rx");
    // Node/JS:     at handle (/srv/router.js:15:7)
    static const std::regex reJs(R"(at (\S+) \((.+):(\d+):\d+\))");
    // gdb/C++: #0  resolveTarget at graph_store.cpp:120
    static const std::regex reGdb(R"(#\d+\s+(\S+) at ([^:\s]+):(\d+))");

    std::istringstream is(text);
    std::string ln;
    while (std::getline(is, ln)) {
        std::smatch m;
        if (std::regex_search(ln, m, rePy)) {
            out.push_back({m[1].str(), m[3].str(), std::stoi(m[2].str())});
        } else if (std::regex_search(ln, m, reJs)) {
            out.push_back({m[2].str(), m[1].str(), std::stoi(m[3].str())});
        } else if (std::regex_search(ln, m, reGdb)) {
            out.push_back({m[2].str(), m[1].str(), std::stoi(m[3].str())});
        }
    }
    return out;
}

// Adjacency edges between consecutive frames (frame i -> frame i+1). Direction
// follows source order of the trace (python: caller->callee).
inline std::vector<RuntimeEdge> buildRuntimeEdges(const std::vector<StackFrame>& f) {
    std::vector<RuntimeEdge> e;
    for (size_t i = 0; i + 1 < f.size(); ++i) {
        e.push_back({f[i].file, f[i].func, f[i + 1].file, f[i + 1].func});
    }
    return e;
}

}  // namespace icmg::core
