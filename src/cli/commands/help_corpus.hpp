#pragma once
// Phase 36 T1: canonical command corpus for `icmg ask`.
// Hand-curated paraphrases per command — boost cosine match recall vs
// just-the-name. Add new entries when shipping new commands.
#include <vector>
#include <string>

namespace icmg::cli {

struct CorpusEntry {
    std::string command;       // suggested invocation
    std::string description;   // canonical description
    std::vector<std::string> paraphrases;
};

inline std::vector<CorpusEntry> helpCorpus() {
    return {
        // --- discovery ---
        {"icmg ask \"<question>\"",
         "natural-language router; cosine-match question against all icmg commands",
         {"how do I", "which command", "what does X do", "find the right tool",
          "search commands"}},
        {"icmg pack \"<task>\"",
         "task-context bundle with relevant memory + symbols + rules",
         {"start a new task", "get oriented", "context for this work",
          "bundle related info"}},
        {"icmg context <file>",
         "single-file context: graph + symbols + neighbors + memory",
         {"understand this file", "what does this file do",
          "imports and callers"}},

        // --- recall / search ---
        {"icmg recall \"<query>\"",
         "BM25 memory search over stored notes",
         {"find a past note", "search memory", "what did I store about X"}},
        {"icmg recall \"<q>\" --semantic",
         "hybrid BM25 + cosine recall (paraphrase-tolerant)",
         {"semantic search", "find related ideas", "fuzzy intent match",
          "paraphrase recall"}},
        {"icmg explain \"<error>\"",
         "match error text against past resolutions",
         {"have I seen this error", "fix this stack trace", "deja-vu error"}},
        {"icmg known-issue match \"<text>\"",
         "lookup recurring-error registry by error pattern",
         {"is this a known issue", "search past fixes"}},

        // --- graph ---
        {"icmg graph symbol <Name>",
         "find symbol by name (function/class/sp/table)",
         {"locate function", "where is class defined", "find a method",
          "symbol lookup"}},
        {"icmg graph callers <Name>",
         "incoming call edges to a symbol",
         {"who uses this", "callers of", "references to"}},
        {"icmg graph callees <Name>",
         "outgoing call edges from a symbol",
         {"what does this call", "callees of", "dependencies"}},
        {"icmg graph reverse-impact <Name> --depth 5",
         "transitive impact closure: who breaks if X changes",
         {"blast radius", "impact analysis", "what depends on this",
          "ripple effect"}},
        {"icmg graph update --since 1d",
         "incremental scan of changed files since N time",
         {"refresh graph", "rescan recent", "update index"}},
        {"icmg graph communities --apply-as-zones",
         "Louvain clustering of file graph; suggest zones",
         {"auto-detect modules", "cluster files", "suggest zones"}},

        // --- session / state ---
        {"icmg wake-up",
         "session-start briefing: recent decisions, fixes, in-progress phases",
         {"what was I doing", "yesterday's work", "session summary",
          "where did I leave off"}},
        {"icmg session save <name>",
         "snapshot active recall context for resume",
         {"save my work", "checkpoint state", "before /clear"}},
        {"icmg index",
         "unified maintenance pipeline: scan + embed + consolidate + decay",
         {"daily cleanup", "rebuild everything", "maintenance pass"}},

        // --- write paths ---
        {"icmg store --topic <X> \"<note>\"",
         "store a memory note under topic prefix",
         {"save this", "remember this", "capture decision"}},
        {"icmg known-issue add \"<pat>\" --fix \"<desc>\"",
         "register error pattern + resolution",
         {"capture fix", "save bug solution", "add to issue registry"}},
        {"icmg memoir add --title T --content-file F",
         "long-form narrative memory (post-mortem, design rationale)",
         {"write essay", "post-mortem", "architecture rationale",
          "long-form note"}},
        {"icmg memoir refine <id>",
         "LLM-assisted incremental rewrite of existing memoir",
         {"improve memoir", "refine my note", "incremental edit"}},

        // --- workflow / quality ---
        {"icmg review --base main",
         "PR pre-flight: parity + lint-style on git-diff changed files",
         {"check before commit", "lint changes", "pre-PR gate"}},
        {"icmg pr-summary",
         "generate markdown PR description from git + verifications",
         {"write PR description", "summarize commits", "PR body"}},
        {"icmg parity <ref> <new>",
         "symbol-level feature parity check between two files",
         {"missing methods after copy", "file diff symbols",
          "did I forget anything", "clone check"}},
        {"icmg lint-style <file> --ref <ref>",
         "text-pattern UI consistency lint",
         {"style check", "UI consistency", "rule lint"}},
        {"icmg verify --command \"<cmd>\"",
         "run command, record exit/output for audit gate",
         {"record test pass", "audit verification", "ci gate record"}},
        {"icmg discover --apply",
         "scan transcripts for missed icmg-run opportunities; auto-install hook",
         {"agent forgetting", "missed icmg routes", "auto-install hook"}},

        // --- noise reduction ---
        {"icmg run <cmd>",
         "wrap noisy command in Tkil filter (60-90% smaller output)",
         {"filter command output", "noise reduction",
          "long output trimmed"}},
        {"icmg ls [path]",
         "directory listing, dirs first, sizes",
         {"list files", "what's in this dir"}},
        {"icmg summarize <file>",
         "heuristic outline of file (avoids full Read on huge files)",
         {"file outline", "skim large file", "tldr file"}},
        {"icmg diff-summary --ref HEAD~5",
         "symbol-aware git diff summary (vs raw diff)",
         {"compact diff", "review big diff",
          "what changed without 1000 lines"}},

        // --- maintenance ---
        {"icmg memory consolidate",
         "merge near-duplicate memory nodes (cosine ≥ 0.92)",
         {"dedupe memory", "collapse duplicates"}},
        {"icmg memory health",
         "diagnostic: stale, orphan, embed coverage, distribution",
         {"memory stats", "is memory healthy", "diagnostic report"}},
        {"icmg memory decay",
         "reduce importance of stale nodes",
         {"prune old memory", "decay unused"}},
        {"icmg embed memory",
         "build semantic index over memory_nodes",
         {"build embeddings", "enable semantic search"}},

        // --- agent / config ---
        {"icmg agent \"<task>\"",
         "delegate task to LLM with packed context",
         {"ask LLM", "delegate", "AI assist"}},
        {"icmg chat",
         "interactive REPL over agent",
         {"chat mode", "conversation"}},
        {"icmg config set <key> <value>",
         "edit ~/.icmg/config.json",
         {"change setting", "configure"}},
        {"icmg init",
         "bootstrap project: hooks + AGENTS.md routing + embedder",
         {"setup project", "first time", "install hooks"}},
        {"icmg update --check",
         "check for newer icmg release on github",
         {"upgrade", "check version", "newer available"}},
    };
}

} // namespace icmg::cli
