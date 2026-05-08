// Phase 39 T3: `icmg expand` — reverse compressed text using glossary preface
// or persisted glossary by content hash.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../compress/compressor.hpp"
#include "../../compress/glossary_store.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace icmg::cli {

class ExpandCommand : public BaseCommand {
public:
    std::string name()        const override { return "expand"; }
    std::string description() const override {
        return "Reverse `icmg compress` output via glossary";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg expand [options] [<file>]\n\n"
            "Options:\n"
            "  --hash <H>            Load glossary by content hash from db\n"
            "  --lenient             Leave unknown aliases as-is (default: strict error)\n"
            "  -o <file>             Write to file\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool strict = !hasFlag(args, "--lenient");
        std::string hash = flagValue(args, "--hash");
        std::string out_path = flagValue(args, "-o");

        std::string input = readInput(args);
        if (input.empty()) {
            std::cerr << "icmg expand: empty input\n";
            return 1;
        }

        std::map<std::string,std::string> glossary;
        std::string body;
        if (compress::Compressor::parsePreface(input, &glossary, &body)) {
            // Found inline preface; use that.
        } else if (!hash.empty()) {
            try {
                auto& cfg = core::Config::instance();
                core::Db db(cfg.projectDbPath("."));
                compress::GlossaryStore store(db);
                glossary = store.load(hash);
                body = input;
            } catch (...) {}
        } else {
            // No preface, no hash — treat as raw text and pass-through.
            body = input;
        }

        std::string err;
        std::string expanded = compress::Compressor::expand(body, glossary, strict, &err);
        if (strict && !err.empty()) {
            std::cerr << "icmg expand: " << err << "\n";
            return 2;
        }

        std::ostream* os = &std::cout;
        std::ofstream of;
        if (!out_path.empty()) {
            of.open(out_path, std::ios::binary);
            if (!of) { std::cerr << "icmg expand: open " << out_path << " failed\n"; return 3; }
            os = &of;
        }
        *os << expanded;
        return 0;
    }

private:
    static std::string readInput(const std::vector<std::string>& args) {
        std::string path;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "-o" || a == "--hash") { ++i; continue; }
            if (!a.empty() && a[0] == '-') continue;
            path = a;
        }
        if (!path.empty()) {
            std::ifstream f(path, std::ios::binary);
            if (!f) return {};
            std::ostringstream ss; ss << f.rdbuf();
            return ss.str();
        }
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        return ss.str();
    }
};

ICMG_REGISTER_COMMAND("expand", ExpandCommand);

} // namespace icmg::cli
