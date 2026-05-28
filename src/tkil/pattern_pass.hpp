// v1.56 T1 Stage 3: pattern-collapse profiles.
//
// Each profile inspects a command line (e.g. "npm install"), decides whether
// it matches, and if so transforms the raw output by collapsing repetitive
// patterns to a single summary line. Profiles self-register via the
// ICMG_REGISTER_PATTERN_PROFILE macro and live in
// src/tkil/pattern_profiles/<tool>_profile.cpp.
//
// Error/fatal lines are NEVER touched by any profile (delegated to the
// dedup-pass allowlist via isAlwaysVerbatim).

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace icmg::tkil {

struct PatternProfile {
    std::string name;                                       // e.g. "npm install"
    std::function<bool(const std::string&)> matches;        // matches cmd line
    std::function<std::string(const std::string&)> apply;   // input -> output
};

// Apply the first matching profile to `input`. Returns input unchanged if no
// profile matches the cmdline.
std::string patternPass(const std::string& input, const std::string& cmdline);

// Register a profile at static-init time.
void registerPatternProfile(const PatternProfile& p);

#define ICMG_REGISTER_PATTERN_PROFILE(VAR, NAME, MATCHFN, APPLYFN)             \
    namespace {                                                                \
    struct _pp_reg_##VAR {                                                     \
        _pp_reg_##VAR() {                                                      \
            ::icmg::tkil::registerPatternProfile(                              \
                {NAME, MATCHFN, APPLYFN});                                     \
        }                                                                      \
    } _pp_reg_inst_##VAR;                                                      \
    }

} // namespace icmg::tkil
