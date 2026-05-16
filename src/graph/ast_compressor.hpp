#pragma once
// ast_compressor.hpp — regex-based AST body elision for icmg pack --compress-ast.
//
// Supported languages: c, cpp, python, js, ts, go, rust
// For unsupported languages, compressAst() returns the source unchanged.
//
// Strategy (v1.3 — regex-based):
//   C/C++  : detect function/struct signatures via balanced-brace scan,
//             replace bodies with { /* ... */ }.
//   Python : detect def/class lines, replace indented body with "    ...".
//   Go     : same balanced-brace scan as C/C++.
//   Rust   : same balanced-brace scan as C/C++.
//   JS/TS  : same balanced-brace scan as C/C++.
//
// v1.4 plan: replace regex scan with real tree-sitter node queries once
// grammar bindings are wired per-language in BaseSymbolExtractor.
//
// Fail-soft: on any internal error, compressAst() returns the original source.

#include <string>

namespace icmg::graph {

/// Compress source by eliding function/class bodies for the given language.
/// @param source  UTF-8 source text.
/// @param lang    Language tag: "c", "cpp", "python", "js", "ts", "go", "rust".
///                Any other value → source returned verbatim.
/// @return        Compressed source. Always valid UTF-8. May equal source on
///                failure or unsupported lang.
std::string compressAst(const std::string& source, const std::string& lang);

/// Detect language from file extension. Returns lowercase tag or "" if unknown.
std::string detectLangFromExtension(const std::string& path);

} // namespace icmg::graph
