// `icmg calc "<expr>"` -- offline arithmetic/stat evaluator.
//
// Closes the python/node-throwaway RAW=1 escape (2026-06-28 audit): the model
// shelled out ~40x just to compute a percentage, a power, or an average. This
// gives icmg a native compute primitive so no subprocess is needed.
//
// All evaluation logic lives in calc_eval.hpp (pure, unit-tested). This file is
// only argv glue + output formatting.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../calc_eval.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class CalcCommand : public BaseCommand {
public:
    std::string name()        const override { return "calc"; }
    std::string description() const override {
        return "Evaluate an arithmetic/stat expression (offline, no python needed)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg calc \"<expression>\" [--json]\n\n"
            "Offline expression evaluator -- use instead of shelling out to python/node\n"
            "for a quick number. Pure + deterministic.\n\n"
            "Operators:  + - * / %  ** (power, right-assoc)  ( )  unary -\n"
            "Functions:  sqrt abs floor ceil round log ln exp sin cos tan\n"
            "            min max sum avg   (last four are variadic)\n"
            "Constants:  pi e\n\n"
            "Examples:\n"
            "  icmg calc \"2**32\"            -> 4294967296\n"
            "  icmg calc \"50/200*100\"       -> 25\n"
            "  icmg calc \"avg(1,2,3,4)\"     -> 2.5\n"
            "  icmg calc \"sqrt(144)\" --json -> {\"ok\":true,\"value\":12}\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        bool json = hasFlag(args, "--json");

        // Join all non-flag args so both `calc "2+3"` and `calc 2 + 3` work.
        std::ostringstream expr;
        bool first = true;
        for (const auto& a : args) {
            if (a == "--json" || a == "--help") continue;
            if (!first) expr << ' ';
            expr << a;
            first = false;
        }

        if (first) {  // no expression given
            if (json) std::cout << "{\"ok\":false,\"error\":\"no expression\"}\n";
            else      std::cerr << "icmg calc: no expression given (try: icmg calc \"2+3\")\n";
            return 2;
        }

        CalcResult r = evalExpr(expr.str());

        if (json) {
            std::cout << "{\"ok\":" << (r.ok ? "true" : "false");
            if (r.ok) std::cout << ",\"value\":" << formatCalc(r.value);
            else      std::cout << ",\"error\":\"" << r.error << "\"";
            std::cout << "}\n";
            return r.ok ? 0 : 1;
        }

        if (r.ok) { std::cout << formatCalc(r.value) << "\n"; return 0; }
        std::cerr << "icmg calc: " << r.error << "\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("calc", CalcCommand);

}  // namespace icmg::cli
