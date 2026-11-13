// 2026-11-11: ACT-R base-level activation as an opt-in recency x frequency
// multiplier for memory ranking (env ICMG_RECENCY=actr; default unchanged).
// Research: docs/plans/2026-11-11-token-memory-research.md item 5.
//
// Human forgetting follows a power law (Wixted & Ebbesen 1991; ACT-R,
// Anderson & Lebiere 1998). Full ACT-R needs every use time; Petrov (2006)
// approximates it from n uses, last-use age t_n and lifetime L (hours):
//   B = ln( t_n^-d + (n-1) * (L^(1-d) - t_n^(1-d)) / ((1-d) * (L - t_n)) )
// The multiplier is the ACT-R retrieval probability with threshold 0, s = 1:
//   P = 1 / (1 + exp(-B))       in (0, 1)
// It replaces BOTH recency (accessAwareDecay) and log(2 + freq) in the Scorer.
// Honest note: no LLM-memory benchmark proves this beats the default; the
// LME / LoCoMo harness has no access history. Opt-in until replay data says so.
#pragma once
#include <cstdint>

namespace icmg::imem {

constexpr double      kActrDecay       = 0.5;    // d, standard ACT-R value
constexpr double      kActrMinAgeHours = 1.0 / 60.0;  // 1 minute floor (t^-d finite)
constexpr double      kActrUnknownMult = 0.5;    // no timestamps: same as legacy recencyDecay
constexpr const char* kActrRecencyEnv  = "ICMG_RECENCY";
constexpr const char* kActrRecencyOn   = "actr";

// B for n uses (n < 1 treated as 1), last use ageHours ago, first use
// lifetimeHours ago (clamped to >= ageHours).
double actrBaseLevel(int n, double ageHours, double lifetimeHours);

// 1 / (1 + exp(-B)).
double actrRecencyMult(int n, double ageHours, double lifetimeHours);

// From epoch seconds: last use (0 -> created_at), creation (0 -> last use).
// Both unknown -> kActrUnknownMult.
double actrRecencyMultAt(int n, int64_t lastUsed, int64_t createdAt, int64_t now);

bool    actrRecencyFromEnv();   // ICMG_RECENCY=actr
int64_t actrNowSec();

}  // namespace icmg::imem
