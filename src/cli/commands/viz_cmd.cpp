#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../viz/graph_serializer.hpp"
#include "../../viz/html_template.hpp"
#include "../../viz/dot_exporter.hpp"
#include "../../viz/gexf_exporter.hpp"
#include "../../viz/graphml_exporter.hpp"
#include "../../viz/browser_opener.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace icmg::cli {

class VizCommand : public BaseCommand {
public:
    std::string name()        const override { return "viz"; }
    std::string description() const override {
        return "Generate interactive graph visualization (HTML/DOT/GEXF/GraphML)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg viz [options]\n\n"
            "Options:\n"
            "  --output <file>        Output file (default: icmg-viz/index.html)\n"
            "  --format <fmt>         Output format: html (default)|dot|gexf|graphml\n"
            "  --no-open              Don't open browser after generating HTML\n"
            "  --filter-lang <langs>  Comma-separated language filter (e.g. cpp,go)\n"
            "  --community <id>       Show only one community\n"
            "  --estimate             Show estimated output size and exit\n\n"
            "Examples:\n"
            "  icmg viz\n"
            "  icmg viz --output ./viz/index.html\n"
            "  icmg viz --format dot > graph.dot\n"
            "  icmg viz --filter-lang cpp,go --no-open\n"
            "  icmg viz --community 3\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        std::string fmt        = flagValue(args, "--format", "html");
        std::string outFile    = flagValue(args, "--output");
        std::string filterLang = flagValue(args, "--filter-lang");
        std::string community  = flagValue(args, "--community");
        bool noOpen            = hasFlag(args, "--no-open");
        bool estimate          = hasFlag(args, "--estimate");

        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);

        // Count nodes for large-graph warning (A1)
        int nodeCount = 0;
        db.query("SELECT COUNT(*) FROM graph_nodes", {},
                 [&](const core::Row& r) {
                     if (!r.empty()) try { nodeCount = std::stoi(r[0]); } catch (...) {}
                 });

        if (estimate) {
            // Rough estimate: ~600KB Cytoscape.js + ~1KB per node
            long estKb = 600 + nodeCount;
            std::cout << "Estimated output size: ~" << estKb << " KB"
                      << " (" << nodeCount << " nodes, Cytoscape.js embedded via CDN)\n";
            std::cout << "[Continue? icmg viz]\n";
            return 0;
        }

        // Large graph warning
        if (fmt == "html" && nodeCount > 2000) {
            std::cerr << "Warning: " << nodeCount << " nodes detected — large graph.\n"
                      << "Recommendation: use --format gexf and open with Gephi,\n"
                      << "or --community <id> to visualize one community at a time.\n"
                      << "Continuing with HTML generation...\n";
        }

        // Parse lang filter
        std::vector<std::string> langs;
        if (!filterLang.empty()) {
            std::istringstream ss(filterLang);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                while (!tok.empty() && tok.front() == ' ') tok.erase(0,1);
                while (!tok.empty() && tok.back()  == ' ') tok.pop_back();
                if (!tok.empty()) langs.push_back(tok);
            }
        }

        std::cerr << "Generating visualization...\n";

        // DOT format
        if (fmt == "dot") {
            viz::DotExporter exp(db);
            std::string dot = exp.toDot(langs);
            if (!outFile.empty()) {
                std::ofstream f(outFile);
                if (!f) { std::cerr << "Cannot open: " << outFile << "\n"; return 1; }
                f << dot;
                std::cerr << "  Output: " << outFile << "\n";
            } else {
                std::cout << dot;
            }
            return 0;
        }

        // GEXF format
        if (fmt == "gexf") {
            viz::GexfExporter exp(db);
            std::string gexf = exp.toGexf();
            if (!outFile.empty()) {
                std::ofstream f(outFile);
                if (!f) { std::cerr << "Cannot open: " << outFile << "\n"; return 1; }
                f << gexf;
                std::cerr << "  Output: " << outFile << "\n";
            } else {
                std::cout << gexf;
            }
            return 0;
        }

        // GraphML format
        if (fmt == "graphml") {
            viz::GraphmlExporter exp(db);
            std::string gml = exp.toGraphml();
            if (!outFile.empty()) {
                std::ofstream f(outFile);
                if (!f) { std::cerr << "Cannot open: " << outFile << "\n"; return 1; }
                f << gml;
                std::cerr << "  Output: " << outFile << "\n";
            } else {
                std::cout << gml;
            }
            return 0;
        }

        // HTML (default)
        viz::GraphSerializer ser(db);
        auto data = ser.serialize(langs, community);

        std::cerr << "  Nodes: " << data.nodes.size()
                  << "  Edges: " << data.edges.size()
                  << "  Communities: " << data.community_colors.size() << "\n";

        std::string json = ser.toJson(data);

        // Determine project title
        std::string title = "icmg graph";
        try {
            namespace fs = std::filesystem;
            title = fs::current_path().filename().string();
        } catch (...) {}

        std::string html = viz::buildHtml(json, title);

        // Output path
        if (outFile.empty()) {
            namespace fs = std::filesystem;
            fs::create_directories("icmg-viz");
            outFile = "icmg-viz/index.html";
        } else {
            namespace fs = std::filesystem;
            fs::path p(outFile);
            if (p.has_parent_path()) {
                fs::create_directories(p.parent_path());
            }
        }

        {
            std::ofstream f(outFile);
            if (!f) {
                std::cerr << "Cannot write to: " << outFile << "\n";
                return 1;
            }
            f << html;
        }

        // Size
        std::error_code ec;
        auto sz = std::filesystem::file_size(outFile, ec);
        std::string sizeStr;
        if (!ec) {
            if (sz < 1024*1024) sizeStr = std::to_string(sz/1024) + " KB";
            else sizeStr = std::to_string(sz/(1024*1024)) + " MB";
        }

        std::cerr << "  Output: " << outFile;
        if (!sizeStr.empty()) std::cerr << " (" << sizeStr << ")";
        std::cerr << "\n";

        if (!noOpen) {
            // Get absolute path for browser
            namespace fs = std::filesystem;
            std::string absPath;
            try { absPath = fs::absolute(outFile).string(); } catch (...) { absPath = outFile; }
            std::cerr << "Opening in browser...\n";
            viz::openInBrowser(absPath);
        }

        return 0;
    }
};

ICMG_REGISTER_COMMAND("viz", VizCommand);

} // namespace icmg::cli
