#include "ref_writeback.hpp"

#include "../core/config.hpp"
#include "../core/db.hpp"
#include "../core/path_u8.hpp"
#include "../core/path_utils.hpp"
#include "../imem/ai_ref_store.hpp"
#include "../imem/memory_node.hpp"
#include "../imem/memory_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace icmg::cli::refflow {

namespace {

// Substrings that mark a Bash command as a test/build check. One place.
const char* const kVerifyMarkers[] = {
    "ctest", "pytest", "npm test", "npm run test", "npm run build", "yarn test",
    "pnpm test", "cargo test", "cargo build", "go test", "go build", "dotnet test",
    "dotnet build", "mvn test", "gradle test", "make test", "jest", "vitest",
    "icmg_test", "icmg verify", "build.ps1", "cmake --build", "tsc --noemit",
};

const char* const kEditTools[] = {"Edit", "Write", "MultiEdit", "NotebookEdit"};

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool isEditTool(const std::string& n) {
    for (auto* t : kEditTools) if (n == t) return true;
    return false;
}

// Real user prompt text, or "" when the user line is only tool results.
std::string promptText(const json& msg) {
    if (!msg.contains("content")) return "";
    const auto& c = msg["content"];
    if (c.is_string()) return c.get<std::string>();
    if (!c.is_array()) return "";
    std::string text;
    for (const auto& b : c) {
        if (!b.is_object()) continue;
        std::string t = b.value("type", "");
        if (t == "tool_result") return "";
        if (t == "text") text += b.value("text", "");
    }
    return text;
}

std::string firstLine(const std::string& s) {
    std::string t = trim(s);
    size_t nl = t.find('\n');
    return trim(nl == std::string::npos ? t : t.substr(0, nl));
}

std::string readTail(const std::string& path, size_t cap) {
    std::ifstream f(core::u8ToPath(path), std::ios::binary);
    if (!f) return "";
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    std::streamoff start = size > (std::streamoff)cap ? size - (std::streamoff)cap : 0;
    f.seekg(start);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (start > 0) {                       // drop the partial first line
        size_t nl = data.find('\n');
        data = nl == std::string::npos ? "" : data.substr(nl + 1);
    }
    return data;
}

}  // namespace

bool isVerifyCommand(const std::string& cmd) {
    const std::string c = lower(cmd);
    for (auto* m : kVerifyMarkers) if (c.find(m) != std::string::npos) return true;
    return false;
}

TurnScan scanTranscriptTurn(const std::string& jsonl) {
    std::vector<json> lines;
    std::istringstream in(jsonl);
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) continue;
        try { lines.push_back(json::parse(line)); } catch (...) {}
    }

    // Last real prompt = start of the turn; problem = last substantive prompt.
    long turn_start = -1;
    TurnScan s;
    for (long i = (long)lines.size() - 1; i >= 0; --i) {
        const auto& j = lines[i];
        if (!j.is_object() || j.value("type", "") != "user" || !j.contains("message")) continue;
        std::string p = trim(promptText(j["message"]));
        if (p.empty()) continue;
        if (turn_start < 0) turn_start = i;
        if (p.size() >= kRefMinProblemLen) { s.problem = p; break; }
    }
    if (turn_start < 0) return s;

    // Walk the turn: tool uses, their results, last assistant text.
    std::vector<std::pair<std::string, std::string>> verifies;   // id -> command, in order
    std::vector<std::pair<std::string, bool>> results;           // id -> is_error
    for (size_t i = (size_t)turn_start; i < lines.size(); ++i) {
        const auto& j = lines[i];
        if (!j.is_object() || !j.contains("message")) continue;
        const auto& msg = j["message"];
        if (!msg.is_object() || !msg.contains("content") || !msg["content"].is_array()) continue;
        const std::string type = j.value("type", "");
        for (const auto& b : msg["content"]) {
            if (!b.is_object()) continue;
            const std::string bt = b.value("type", "");
            if (type == "assistant" && bt == "tool_use") {
                const std::string name = b.value("name", "");
                const json in = b.contains("input") && b["input"].is_object() ? b["input"] : json::object();
                if (isEditTool(name)) {
                    std::string fp = in.value("file_path", in.value("notebook_path", ""));
                    if (!fp.empty() && std::find(s.edited_files.begin(), s.edited_files.end(), fp)
                                           == s.edited_files.end())
                        s.edited_files.push_back(fp);
                } else if (name == "Bash" || name == "PowerShell") {
                    std::string cmd = in.value("command", "");
                    if (isVerifyCommand(cmd)) verifies.push_back({b.value("id", ""), cmd});
                }
            } else if (type == "assistant" && bt == "text") {
                std::string t = firstLine(b.value("text", ""));
                if (!t.empty()) s.outcome = capUtf8(t, kRefOutcomeCap);
            } else if (type == "user" && bt == "tool_result") {
                results.push_back({b.value("tool_use_id", ""), b.value("is_error", false)});
            }
        }
    }

    // Verdict per verify command (no result recorded = unknown, ignored).
    for (const auto& [id, cmd] : verifies) {
        auto it = std::find_if(results.begin(), results.end(),
                               [&](const auto& r) { return r.first == id; });
        if (it == results.end()) continue;
        if (it->second) { s.verify_failed_after = true; }
        else { s.verified_ok = true; s.verify_failed_after = false; s.verify_cmd = cmd; }
    }
    return s;
}

WritebackDecision runRefWriteback(const WritebackContext& ctx) {
    WritebackSignals sig;
    sig.disabled = ctx.disabled || ctx.ref_db == nullptr;
    sig.stored_this_session = ctx.stored_this_session;
    if (sig.disabled) return decideWriteback(sig);
    try {
        TurnScan scan = scanTranscriptTurn(readTail(ctx.transcript_path, kRefTranscriptTailBytes));
        sig.changed = !scan.edited_files.empty() && !scan.problem.empty();
        sig.verified_ok = scan.verified_ok;
        sig.verify_failed_after = scan.verify_failed_after;
        RefContent rc = buildRefContent(scan.problem, scan.edited_files, scan.verify_cmd, scan.outcome);
        imem::AiRefStore store(*ctx.ref_db);
        if (sig.changed && !rc.problem.empty()) sig.duplicate = store.isDuplicate(rc.problem);
        WritebackDecision d = decideWriteback(sig);
        if (d != WritebackDecision::Store) return d;

        store.add({rc.problem, rc.solution, ctx.project, "", scan.verify_cmd});
        if (ctx.project_db) {
            try {
                imem::MemoryStore mem(*ctx.project_db);
                imem::MemoryNode node;
                node.topic = kRefProjectTopic;
                node.content = "Problem: " + rc.problem + "\nSolution: " + rc.solution;
                node.source = "ref-writeback";
                mem.store(node);
            } catch (...) {}   // project write is best-effort; the Model-AI ref is stored
        }
        return d;
    } catch (...) {
        return WritebackDecision::SkipDisabled;
    }
}

std::string runRefWritebackHook(const std::string& stop_stdin) {
    try {
        if (writebackDisabledByEnv(std::getenv(kRefWritebackEnv))) return "skip-disabled";
        json j = json::parse(stop_stdin);
        std::string tp = j.value("transcript_path", "");
        if (tp.empty()) return "";
        std::string cwd = j.value("cwd", "");
        if (cwd.empty()) cwd = fs::current_path().string();
        std::string sid = j.value("session_id", "nosession");
        for (auto& c : sid) if (!std::isalnum((unsigned char)c) && c != '-') c = '_';

        fs::path counter = core::u8ToPath(core::icmgGlobalDir()) / "ref-writeback" / (sid + ".count");
        int stored = 0;
        { std::ifstream f(counter); f >> stored; }

        WritebackContext ctx;
        ctx.transcript_path = tp;
        ctx.project = core::pathU8(core::u8ToPath(cwd).filename());
        ctx.ref_db = &imem::modelAiRefDb();
        ctx.stored_this_session = stored;
        std::unique_ptr<core::Db> pdb;
        if (fs::is_directory(core::u8ToPath(cwd) / ".icmg")) {
            pdb = std::make_unique<core::Db>(core::Config::instance().projectDbPath(cwd));
            ctx.project_db = pdb.get();
        }
        WritebackDecision d = runRefWriteback(ctx);
        if (d == WritebackDecision::Store) {
            std::error_code ec;
            fs::create_directories(counter.parent_path(), ec);
            std::ofstream f(counter, std::ios::trunc);
            f << (stored + 1);
        }
        return writebackDecisionName(d);
    } catch (...) {
        return "";
    }
}

}  // namespace icmg::cli::refflow
