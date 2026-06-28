// Pure head/tail -> line-range resolution for `icmg context --head/--tail`.
//
// Extracted from bundle_cmd.cpp (2026-06-28) so the boundary math is unit-tested
// in isolation. Closes the head/tail RAW=1 escape: the model used to shell out to
// `head -N` / `tail -N`; now `icmg context <file> --head N` / `--tail N` covers it.
#ifndef ICMG_HEADTAIL_RANGE_HPP
#define ICMG_HEADTAIL_RANGE_HPP

namespace icmg::cli {

struct LineRange { int start = 0; int end = 0; };

// Given a file's total line count, resolve --head N (first N) or --tail N
// (last N) into an inclusive 1-based [start,end] range. head_n/tail_n <= 0
// means "not requested". head takes precedence if both are set. Returns {0,0}
// when neither is requested or total < 1.
inline LineRange resolveHeadTail(int total, int head_n, int tail_n) {
    LineRange r;
    if (total < 1) return r;
    if (head_n > 0) {
        r.start = 1;
        r.end   = head_n < total ? head_n : total;
    } else if (tail_n > 0) {
        r.start = total - tail_n + 1;
        if (r.start < 1) r.start = 1;
        r.end = total;
    }
    return r;
}

}  // namespace icmg::cli

#endif  // ICMG_HEADTAIL_RANGE_HPP
