#pragma once
// Pre-task recall gate — PRE-CHANGE enforcement (2026-06-11).
//
// Third leg of the discipline trilogy:
//   ritual      — POST-change: you MUST sync after editing (Stop-hook block)
//   discipline  — VISIBILITY: which features did you use this session
//   recall-gate — PRE-task:  you MUST recall/pack BEFORE editing on a complex
//                 task — stops the "dive into edits with zero context" anti-
//                 pattern (PreToolUse:Edit deny).
//
// PURE CORE — no I/O — unit-testable. Complexity comes from classifyIntent
// (think_directive, already tested); "recalled this turn" is a per-turn marker
// set when recall/pack/context ran. The command wraps both with markers + the
// PreToolUse deny JSON.

#include <string>

namespace icmg::cli {

struct RecallGateVerdict {
    bool block = false;
    std::string reason;
};

// Block only when the task is COMPLEX and no recall/pack happened this turn.
// Simple tasks never block (an obvious one-line edit needs no context bundle);
// a complex task that already recalled is fine. Conservative by design: the
// false-positive cost (a needless recall) is far cheaper than the false-
// negative (editing a subsystem blind).
inline RecallGateVerdict recallGateVerdict(bool taskComplex, bool recalledThisTurn) {
    RecallGateVerdict v;
    if (taskComplex && !recalledThisTurn) {
        v.block  = true;
        v.reason = "complex task started without an icmg recall/pack this turn";
    }
    return v;
}

}  // namespace icmg::cli
