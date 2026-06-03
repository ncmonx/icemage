// Phase 47 T1+T2: `icmg ingest <image>` — OCR via Python sidecar + cache.
//
// Routes screenshot/image through pytesseract for text extraction. 90%+ token
// saving vs Anthropic vision call when image is text-heavy (code/terminal/
// error msg). Cache 7d per image content hash.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../graph/graph_store.hpp"   // v2.0.0 Phase 3: media -> graph node
#include "../../graph/media_node.hpp"    // v2.0.0 Phase 3: buildMediaNode
#include "../../core/structural_trim.hpp" // v2.0.0 C7: doc intake-trim
#include "../../core/compress_select.hpp" // v2.0.0 C7: budget-fit salience
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class IngestCommand : public BaseCommand {
public:
    std::string name()        const override { return "ingest"; }
    std::string description() const override {
        return "OCR/extract image content (Python sidecar) + cache; 90% saving vs vision";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg ingest <image-path> [options]\n\n"
            "Options:\n"
            "  --raw                 Skip OCR; print metadata only\n"
            "  --refresh             Bypass cache, re-OCR\n"
            "  --min-chars N         Below this → fall back to vision-recommended (default 30)\n"
            "  --ttl N               Cache TTL seconds (default 604800 = 7d)\n"
            "  --no-graph            Do not record the media as a graph node\n"
            "  --json                Machine-readable output\n"
            "  -o <file>             Write to file\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || args.empty()) { usage(); return 0; }
        std::string path;
        for (auto& a : args) {
            if (!a.empty() && a[0] != '-') { path = a; break; }
        }
        if (path.empty()) { std::cerr << "icmg ingest: requires <image-path>\n"; return 1; }
        if (!fs::exists(path)) {
            std::cerr << "icmg ingest: file not found: " << path << "\n"; return 1;
        }

        bool raw       = hasFlag(args, "--raw");
        bool refresh   = hasFlag(args, "--refresh");
        bool json_out  = hasFlag(args, "--json");
        std::string out_path = flagValue(args, "-o");
        bool no_graph  = hasFlag(args, "--no-graph");
        int min_chars = 30;
        int ttl = 604800;
        try { min_chars = std::stoi(flagValue(args, "--min-chars", "30")); } catch (...) {}
        try { ttl = std::stoi(flagValue(args, "--ttl", "604800")); } catch (...) {}

        // Read bytes + hash.
        std::ifstream f(path, std::ios::binary);
        std::ostringstream ss; ss << f.rdbuf();
        std::string bytes = ss.str();
        if (bytes.empty()) { std::cerr << "icmg ingest: empty file\n"; return 1; }

        std::string hash = fnv1a_hex(bytes);
        int64_t bytes_in = (int64_t)bytes.size();

        // v2.0.0 C7: text-document intake-trim (path-routed; UI attachments uninterceptable).
        // --doc forces it; otherwise auto-detect by text extension. Strips boilerplate/dup
        // lines (structuralTrim) then optional budget-fit salience selection. No OCR/sidecar.
        {
            std::string ext;
            auto dot = path.find_last_of('.');
            if (dot != std::string::npos) {
                ext = path.substr(dot + 1);
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            }
            static const char* TEXT_EXT[] = {"txt","md","markdown","log","json","yaml",
                                             "yml","xml","csv","rst","ini","toml","cfg"};
            bool isText = false;
            for (auto* e : TEXT_EXT) if (ext == e) { isText = true; break; }
            if (hasFlag(args, "--doc") || isText) {
                // #10: reject binary content (a NUL byte means not a UTF-8 text doc) so we
                // never emit garbage bytes that break downstream JSON / the model's input.
                if (bytes.find('\0') != std::string::npos) {
                    std::cerr << "icmg ingest --doc: binary content detected (NUL byte); "
                                 "not a text document — skipping.\n";
                    return 1;
                }
                std::string trimmed = core::structuralTrim(bytes);
                int budget = 0;
                try { budget = std::stoi(flagValue(args, "--budget", "0")); } catch (...) {}
                if (budget > 0) {
                    std::vector<std::string> lines; std::vector<double> scores;
                    std::istringstream ls(trimmed); std::string ln;
                    while (std::getline(ls, ln)) {
                        lines.push_back(ln);
                        scores.push_back(core::infoScore(ln));
                    }
                    trimmed = core::selectByBudget(lines, scores, (size_t)budget, "\n");
                }
                int64_t out_bytes = (int64_t)trimmed.size();
                if (!out_path.empty()) { std::ofstream of(out_path, std::ios::binary); of << trimmed; }
                if (json_out) {
                    nlohmann::json j;
                    j["kind"] = "document"; j["bytes_in"] = bytes_in;
                    j["bytes_out"] = out_bytes; j["text"] = trimmed;
                    std::cout << j.dump(2) << "\n";
                } else {
                    std::cout << trimmed << "\n";
                    std::cerr << "[ingest --doc] " << bytes_in << "B -> " << out_bytes << "B ("
                              << (bytes_in ? (100 - out_bytes * 100 / bytes_in) : 0)
                              << "% trimmed)\n";
                }
                return 0;
            }
        }

        if (raw) {
            return emitMetadata(hash, bytes_in, json_out, out_path);
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        // Cache lookup unless --refresh.
        if (!refresh) {
            std::string text;
            int conf = 0;
            int64_t now = (int64_t)std::time(nullptr);
            try {
                db.query("SELECT ocr_text, ocr_confidence FROM image_cache "
                         "WHERE image_hash = ? AND expires_at > ?",
                         {hash, std::to_string(now)},
                         [&](const core::Row& r){
                             if (r.size() >= 2) {
                                 text = r[0];
                                 try { conf = std::stoi(r[1]); } catch (...) {}
                             }
                         });
                if (!text.empty()) {
                    db.run("UPDATE image_cache SET hit_count = hit_count + 1 WHERE image_hash = ?",
                           {hash});
                    if (!no_graph) recordMediaNode(db, path, text);
                    return emitOcr(text, conf, hash, bytes_in, /*from_cache*/ true,
                                   min_chars, json_out, out_path, /*elapsed_ms*/ 0);
                }
            } catch (...) {}
        }

        // Run OCR via sidecar.
        auto t0 = std::chrono::steady_clock::now();
        std::string text = runOcr(path);
        auto t1 = std::chrono::steady_clock::now();
        int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        if (text.empty()) {
            std::cerr << "[icmg ingest] OCR returned empty (sidecar missing? "
                      << "Install: pip install pytesseract Pillow + tesseract binary)\n";
            return emitMetadata(hash, bytes_in, json_out, out_path);
        }

        // Heuristic confidence: text length vs image bytes ratio + alphanumeric ratio.
        int conf = computeConfidence(text);

        // Cache store (best-effort).
        try {
            int64_t expires = (int64_t)std::time(nullptr) + ttl;
            db.run("INSERT OR REPLACE INTO image_cache "
                   "(image_hash, bytes, ocr_text, ocr_confidence, expires_at) "
                   "VALUES (?,?,?,?,?)",
                   {hash, std::to_string(bytes_in), text,
                    std::to_string(conf), std::to_string(expires)});
        } catch (...) {}

        if (!no_graph) recordMediaNode(db, path, text);
        return emitOcr(text, conf, hash, bytes_in, /*from_cache*/ false,
                       min_chars, json_out, out_path, elapsed_ms);
    }

private:
    static std::string fnv1a_hex(const std::string& s) {
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        char buf[17]; std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
        return buf;
    }

    // v2.0.0 Phase 3: record ingested media as a first-class graph node so
    // context/graph/zone surface it. Never fails the ingest on graph error.
    static void recordMediaNode(core::Db& db, const std::string& path,
                                const std::string& text) {
        if (text.empty()) return;
        try {
            std::string mt = "image";
            auto dot = path.find_last_of('.');
            if (dot != std::string::npos) {
                std::string ext = path.substr(dot + 1);
                for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == "pdf") mt = "pdf";
                else if (ext == "mp4" || ext == "mov" || ext == "mkv" || ext == "webm") mt = "video";
            }
            graph::GraphStore store(db);
            store.upsertNode(graph::buildMediaNode(path, mt, text));
        } catch (...) {}
    }

    // Heuristic confidence based on text density + alphanumeric ratio.
    static int computeConfidence(const std::string& text) {
        if (text.size() < 10) return 0;
        int alnum = 0, total = 0;
        for (unsigned char c : text) {
            if (std::isspace(c)) continue;
            ++total;
            if (std::isalnum(c) || c == '.' || c == ',' || c == '_' || c == '-' || c == '/' || c == '(' || c == ')') ++alnum;
        }
        if (total == 0) return 0;
        int alpha_pct = 100 * alnum / total;
        // Length bonus.
        int length_pct = (int)std::min((size_t)100, text.size() / 10);
        return std::min(100, (alpha_pct + length_pct) / 2);
    }

    std::string runOcr(const std::string& image_path) {
        std::string sidecar = locateSidecar();
        if (sidecar.empty()) return {};

        // Send single-request protocol: ready → image op → parse text.
        // Spawn python with a short script that imports + ocrs path directly.
        std::string py_script =
            "import sys,json;"
            "from PIL import Image;"
            "import pytesseract;"
            "img=Image.open(sys.argv[1]);"
            "print(json.dumps({'text':pytesseract.image_to_string(img)}))";
        std::string cmd = std::string("python3 -c \"") + py_script + "\" \"" + image_path + "\"";
        auto res = core::safeExecShell(cmd, false, 30000);
        if (res.exit_code != 0 || res.out.empty()) {
            // Fallback: try `python` (Windows often).
            cmd = std::string("python -c \"") + py_script + "\" \"" + image_path + "\"";
            res = core::safeExecShell(cmd, false, 30000);
            if (res.exit_code != 0 || res.out.empty()) return {};
        }
        try {
            auto j = nlohmann::json::parse(res.out);
            return j.value("text", "");
        } catch (...) {
            return {};
        }
    }

    std::string locateSidecar() {
        // Project-local first, then ~/.icmg/embed.
        for (auto* p : {"multimodal/icmg_ingest.py", "./multimodal/icmg_ingest.py"}) {
            if (fs::exists(p)) return p;
        }
        const char* h = std::getenv("USERPROFILE");
        if (!h) h = std::getenv("HOME");
        if (h) {
            fs::path p = fs::path(h) / ".icmg" / "embed" / "icmg_ingest.py";
            if (fs::exists(p)) return p.string();
        }
        // Even without sidecar file, the inline python -c script works.
        // Return non-empty to signal "try OCR".
        return "inline";
    }

    int emitOcr(const std::string& text, int conf, const std::string& hash,
                 int64_t bytes_in, bool from_cache, int min_chars,
                 bool json_out, const std::string& out_path, int elapsed_ms) {
        bool low_conf = ((int)text.size() < min_chars) || conf < 50;
        std::ostream* os = &std::cout;
        std::ofstream of;
        if (!out_path.empty()) {
            of.open(out_path, std::ios::binary);
            if (!of) { std::cerr << "icmg ingest: cannot write " << out_path << "\n"; return 2; }
            os = &of;
        }

        if (json_out) {
            nlohmann::json j;
            j["hash"] = hash;
            j["bytes_in"] = bytes_in;
            j["text"] = text;
            j["confidence"] = conf;
            j["from_cache"] = from_cache;
            j["elapsed_ms"] = elapsed_ms;
            j["recommendation"] = low_conf ? "vision" : "text";
            *os << j.dump(2) << "\n";
        } else {
            if (low_conf) {
                *os << "[icmg ingest] confidence " << conf << "% < threshold; "
                    << "vision call recommended.\nExtracted text (may be noisy):\n";
            }
            *os << text;
            if (!text.empty() && text.back() != '\n') *os << "\n";
        }

        int saved_pct = bytes_in > 0
            ? (int)(100 - (100LL * (int64_t)text.size() / bytes_in))
            : 0;
        std::cerr << "[icmg ingest] " << bytes_in << "B image → "
                  << text.size() << "B text "
                  << "(" << saved_pct << "% saved, conf=" << conf << "%)"
                  << (from_cache ? " [cache HIT]" : (" in " + std::to_string(elapsed_ms) + "ms"))
                  << "\n";
        return 0;
    }

    int emitMetadata(const std::string& hash, int64_t bytes_in,
                      bool json_out, const std::string& out_path) {
        std::ostream* os = &std::cout;
        std::ofstream of;
        if (!out_path.empty()) {
            of.open(out_path, std::ios::binary);
            if (!of) return 2;
            os = &of;
        }
        if (json_out) {
            *os << "{\"hash\":\"" << hash << "\",\"bytes\":" << bytes_in << ",\"raw\":true}\n";
        } else {
            *os << "[icmg ingest --raw]\n"
                << "hash: " << hash << "\n"
                << "size: " << bytes_in << " bytes\n"
                << "(use without --raw to OCR)\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("ingest", IngestCommand);

} // namespace icmg::cli
