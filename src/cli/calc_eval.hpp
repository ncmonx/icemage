// `icmg calc` expression evaluator -- pure, offline, deterministic.
//
// Born from the 2026-06-28 RAW=1 escape audit: ~40 throwaway `python`/`node`
// invocations were really just "evaluate this arithmetic quickly" (percentages,
// 2**32, averages of a few numbers). icmg had no compute primitive, so the model
// shelled out. This closes that hole with a tiny recursive-descent evaluator.
//
// Pure (no I/O, no globals) so it is unit-testable in isolation -- mirrors the
// whereami_render.hpp pattern.
//
// Grammar (precedence low -> high):
//   expr    := term (('+' | '-') term)*
//   term    := factor (('*' | '/' | '%') factor)*
//   factor  := ('-' | '+') factor | power     // unary sign binds LOOSER than **
//   power   := primary ('**' factor)?         // right-assoc; exponent may be signed
//   primary := number | '(' expr ')' | ident '(' args ')' | ident
//   args    := expr (',' expr)*
//
// Precedence note: unary minus is intentionally LOOSER than '**', so `-2**2`
// parses as -(2**2) = -4 (Python / math convention), not (-2)**2 = 4.
//
// Functions: sqrt abs floor ceil round log ln exp sin cos tan
//            min max sum avg (variadic for the last four)
// Constants: pi e
#ifndef ICMG_CALC_EVAL_HPP
#define ICMG_CALC_EVAL_HPP

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace icmg::cli {

struct CalcResult {
    bool        ok = false;
    double      value = 0.0;
    std::string error;
};

namespace calc_detail {

struct Parser {
    const std::string& s;
    size_t             i = 0;
    bool               failed = false;
    std::string        err;

    explicit Parser(const std::string& src) : s(src) {}

    void skipWs() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool eof() { skipWs(); return i >= s.size(); }
    char peek() { skipWs(); return i < s.size() ? s[i] : '\0'; }

    void fail(const std::string& m) {
        if (!failed) { failed = true; err = m; }
    }

    double parseExpr() {
        double v = parseTerm();
        while (!failed) {
            char c = peek();
            if (c == '+' || c == '-') {
                ++i;
                double rhs = parseTerm();
                v = (c == '+') ? v + rhs : v - rhs;
            } else break;
        }
        return v;
    }

    double parseTerm() {
        double v = parseFactor();
        while (!failed) {
            char c = peek();
            if (c == '*' && i + 1 < s.size() && s[i + 1] == '*') break;  // '**' handled in power
            if (c == '*' || c == '/' || c == '%') {
                ++i;
                double rhs = parseFactor();
                if (c == '*') v = v * rhs;
                else if (c == '/') {
                    if (rhs == 0.0) { fail("division by zero"); return 0; }
                    v = v / rhs;
                } else {
                    if (rhs == 0.0) { fail("modulo by zero"); return 0; }
                    v = std::fmod(v, rhs);
                }
            } else break;
        }
        return v;
    }

    // Unary sign binds LOOSER than '**' (Python/math convention): -2**2 = -4.
    double parseFactor() {
        char c = peek();
        if (c == '-') { ++i; return -parseFactor(); }
        if (c == '+') { ++i; return parseFactor(); }
        return parsePower();
    }

    double parsePower() {
        double base = parsePrimary();  // base is a primary, not a signed factor
        skipWs();
        if (!failed && i + 1 < s.size() && s[i] == '*' && s[i + 1] == '*') {
            i += 2;
            double exp = parseFactor();  // exponent may be signed (2**-1) + right-assoc
            return std::pow(base, exp);
        }
        return base;
    }

    double parsePrimary() {
        char c = peek();
        if (c == '(') {
            ++i;
            double v = parseExpr();
            if (peek() != ')') { fail("expected ')'"); return 0; }
            ++i;
            return v;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return parseIdent();
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            return parseNumber();
        }
        fail(c == '\0' ? "unexpected end of expression"
                       : std::string("unexpected character '") + c + "'");
        return 0;
    }

    double parseNumber() {
        skipWs();
        size_t start = i;
        while (i < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
                s[i] == 'e' || s[i] == 'E' ||
                ((s[i] == '+' || s[i] == '-') && i > start &&
                 (s[i - 1] == 'e' || s[i - 1] == 'E')))) {
            ++i;
        }
        try {
            std::string tok = s.substr(start, i - start);
            size_t consumed = 0;
            double v = std::stod(tok, &consumed);
            // Reject malformed spans like "1..2" or "1e": stod stops early but we
            // already advanced i over the whole run, so verify it ate everything.
            if (consumed != tok.size()) { fail("invalid number '" + tok + "'"); return 0; }
            return v;
        } catch (...) {
            fail("invalid number");
            return 0;
        }
    }

    std::vector<double> parseArgs() {
        std::vector<double> out;
        if (peek() == ')') return out;
        out.push_back(parseExpr());
        while (!failed && peek() == ',') {
            ++i;
            out.push_back(parseExpr());
        }
        return out;
    }

    double parseIdent() {
        skipWs();
        size_t start = i;
        while (i < s.size() &&
               (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
            ++i;
        }
        std::string id = s.substr(start, i - start);

        // Constants (no parens).
        if (peek() != '(') {
            if (id == "pi") return 3.14159265358979323846;
            if (id == "e")  return 2.71828182845904523536;
            fail("unknown name '" + id + "'");
            return 0;
        }

        // Function call.
        ++i;  // consume '('
        std::vector<double> a = parseArgs();
        if (peek() != ')') { fail("expected ')' after args"); return 0; }
        ++i;

        auto need = [&](size_t n) -> bool {
            if (a.size() != n) { fail(id + "() expects " + std::to_string(n) + " arg(s)"); return false; }
            return true;
        };

        if (id == "sqrt")  { if (!need(1)) return 0; if (a[0] < 0) { fail("sqrt of negative"); return 0; } return std::sqrt(a[0]); }
        if (id == "abs")   { return need(1) ? std::fabs(a[0]) : 0; }
        if (id == "floor") { return need(1) ? std::floor(a[0]) : 0; }
        if (id == "ceil")  { return need(1) ? std::ceil(a[0]) : 0; }
        if (id == "round") { return need(1) ? std::round(a[0]) : 0; }
        if (id == "log")   { if (!need(1)) return 0; if (a[0] <= 0) { fail("log domain"); return 0; } return std::log10(a[0]); }
        if (id == "ln")    { if (!need(1)) return 0; if (a[0] <= 0) { fail("ln domain"); return 0; } return std::log(a[0]); }
        if (id == "exp")   { return need(1) ? std::exp(a[0]) : 0; }
        if (id == "sin")   { return need(1) ? std::sin(a[0]) : 0; }
        if (id == "cos")   { return need(1) ? std::cos(a[0]) : 0; }
        if (id == "tan")   { return need(1) ? std::tan(a[0]) : 0; }

        if (id == "min" || id == "max" || id == "sum" || id == "avg") {
            if (a.empty()) { fail(id + "() needs >=1 arg"); return 0; }
            if (id == "min") { double m = a[0]; for (double x : a) m = x < m ? x : m; return m; }
            if (id == "max") { double m = a[0]; for (double x : a) m = x > m ? x : m; return m; }
            double s2 = 0; for (double x : a) s2 += x;
            return id == "sum" ? s2 : s2 / static_cast<double>(a.size());
        }

        fail("unknown function '" + id + "'");
        return 0;
    }
};

}  // namespace calc_detail

// Evaluate an arithmetic expression. Pure: same input -> same output, no I/O.
inline CalcResult evalExpr(const std::string& expr) {
    CalcResult r;
    bool blank = true;
    for (char c : expr) if (!std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
    if (blank) { r.error = "empty expression"; return r; }

    calc_detail::Parser p(expr);
    double v = p.parseExpr();
    if (p.failed) { r.error = p.err; return r; }
    if (!p.eof()) { r.error = "unexpected trailing input"; return r; }
    if (std::isnan(v)) { r.error = "result is NaN"; return r; }
    if (std::isinf(v)) { r.error = "result is infinite"; return r; }
    r.ok = true;
    r.value = v;
    return r;
}

// Format a numeric result: integer-valued -> no decimals, else trim trailing zeros.
inline std::string formatCalc(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

}  // namespace icmg::cli

#endif  // ICMG_CALC_EVAL_HPP
