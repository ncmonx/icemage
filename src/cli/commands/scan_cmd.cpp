// v1.68 S3: `icmg scan` — walk the filesystem and report leaked secrets.
//
//   icmg scan [path] [--fs] [--redact] [--no-redact] [--json]
//
// Default path = cwd. Exits 1 when any secret is found (CI gate), 0 if clean.
// `--fs` is accepted (and implied) for explicit filesystem-mode intent.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/scan_logic.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class ScanCommand : public BaseCommand {
public:
    std::string name() const override { return "scan"; }
    std::string description() const override {
        return "Scan the filesystem for leaked secrets (API keys, tokens)";
    }
    void usage() const override {
        std::cout
            << "Usage: icmg scan [path] [--fs] [--no-redact] [--json]\n"
            << "  Recursively scan a directory for hardcoded secrets.\n"
            << "  path         directory to scan (default: current dir)\n"
            << "  --fs         filesystem mode (default; explicit intent)\n"
            << "  --no-redact  show the raw matched value (default redacts)\n"
            << "  --json       emit findings as a JSON array\n"
            << "  Exit code 1 if any secret found, 0 if clean.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "-h") || hasFlag(args, "--help")) { usage(); return 0; }

        core::ScanOpts opts;
        opts.redact_preview = !hasFlag(args, "--no-redact");
        const bool json = hasFlag(args, "--json");

        // First non-flag arg = path; default cwd.
        std::string root = ".";
        for (const auto& a : args) {
            if (a.rfind("-", 0) == 0) continue;   // skip flags
            root = a; break;
        }

        auto findings = core::scanTree(root, opts);

        if (json) {
            std::cout << "[";
            for (size_t i = 0; i < findings.size(); ++i) {
                const auto& f = findings[i];
                std::cout << (i ? "," : "")
                          << "{\"path\":\"" << jsonEscape(f.path) << "\","
                          << "\"line\":" << f.line << ","
                          << "\"type\":\"" << jsonEscape(f.type) << "\","
                          << "\"preview\":\"" << jsonEscape(f.preview) << "\"}";
            }
            std::cout << "]\n";
        } else {
            for (const auto& f : findings) {
                std::cout << f.path << ":" << f.line << "  "
                          << f.type << "  " << f.preview << "\n";
            }
            if (findings.empty()) {
                std::cout << "scan: no secrets found under '" << root << "'\n";
            } else {
                std::cout << "\nscan: " << findings.size()
                          << " secret(s) found — review before committing.\n";
            }
        }
        return findings.empty() ? 0 : 1;
    }

private:
    static std::string jsonEscape(const std::string& s) {
        std::string o;
        o.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
                case '"':  o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n";  break;
                case '\r': o += "\\r";  break;
                case '\t': o += "\\t";  break;
                default:   o += c;      break;
            }
        }
        return o;
    }
};

ICMG_REGISTER_COMMAND("scan", ScanCommand);

} // namespace icmg::cli
