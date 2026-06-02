// v2.0.0 externals (API Spec Compilation): `icmg apispec <openapi.json>` — print a
// dense endpoint map instead of feeding a huge OpenAPI doc to the model.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/api_spec.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace icmg::cli {

class ApiSpecCommand : public BaseCommand {
public:
    std::string name()        const override { return "apispec"; }
    std::string description() const override {
        return "Compile an OpenAPI JSON into a dense endpoint map (METHOD path - summary)";
    }
    void usage() const override {
        std::cout << "Usage: icmg apispec <openapi.json>\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return args.empty() ? 2 : 0; }
        std::ifstream f(args[0], std::ios::binary);
        if (!f) { std::cerr << "icmg apispec: cannot open " << args[0] << "\n"; return 1; }
        std::ostringstream ss; ss << f.rdbuf();
        std::string out = core::compactOpenApi(ss.str());
        if (out.empty()) {
            std::cerr << "icmg apispec: no paths found (not an OpenAPI doc, or invalid JSON)\n";
            return 1;
        }
        std::cout << out;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("apispec", ApiSpecCommand);

}  // namespace icmg::cli
