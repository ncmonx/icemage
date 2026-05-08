// Phase 28 T6: `icmg completions <shell>` — emit completion script.
//
// bash/zsh/powershell. Top-level + main subcommands. No per-flag (would
// need schema introspection — defer). Install instructions printed to
// stderr; script body to stdout.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <iostream>
#include <string>

namespace icmg::cli {

// Top-level commands (mirror dispatcher CMDS table — keep in sync).
static const char* TOP_CMDS =
    "store recall memory graph zone run sp abbr rule data project cmd stats "
    "import export doctor known-issue verify phase design wflog context pack "
    "diff-summary explain session summarize budget parallel filter embed agent "
    "chat ls init memoir wiki parity template wake-up discover update feedback "
    "config completions lint-style";

// Subcommands per umbrella.
static const char* GRAPH_SUBS  = "scan update context related impact search list stats "
                                 "orphans cycles communities watch stop watch-status "
                                 "transitive-impact reverse-impact symbol callers callees";
static const char* MEMORY_SUBS = "list show search stats history forget restore purge decay "
                                 "health consolidate extract-patterns";
static const char* SP_SUBS     = "add list show search lint deps diff template impact-table link";
static const char* MEMOIR_SUBS = "add list show search link refine";
static const char* TEMPLATE_SUBS = "extract list show delete apply";
static const char* WIKI_SUBS   = "build serve";

class CompletionsCommand : public BaseCommand {
public:
    std::string name()        const override { return "completions"; }
    std::string description() const override { return "Emit shell completion script (bash/zsh/powershell)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg completions <shell>\n\n"
            "Shells:\n"
            "  bash         Bash completion script\n"
            "  zsh          Zsh completion script (#compdef)\n"
            "  powershell   PowerShell Register-ArgumentCompleter block\n\n"
            "Install:\n"
            "  bash:  icmg completions bash > /etc/bash_completion.d/icmg\n"
            "  zsh:   icmg completions zsh  > ~/.zsh/completions/_icmg\n"
            "  pwsh:  icmg completions powershell >> $PROFILE\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string shell = args[0];
        if (shell == "bash")       emitBash();
        else if (shell == "zsh")   emitZsh();
        else if (shell == "powershell" || shell == "pwsh") emitPwsh();
        else { std::cerr << "completions: unknown shell: " << shell << "\n"; return 1; }
        return 0;
    }

private:
    void emitBash() {
        std::cout <<
R"(# icmg bash completion — auto-generated. Re-run `icmg completions bash` after upgrade.
_icmg_complete() {
    local cur prev words cword
    _init_completion || return
    if [[ $cword -eq 1 ]]; then
        COMPREPLY=( $(compgen -W ")" << TOP_CMDS << R"(" -- "$cur") )
        return
    fi
    case "${words[1]}" in
        graph)    COMPREPLY=( $(compgen -W ")" << GRAPH_SUBS << R"(" -- "$cur") );;
        memory)   COMPREPLY=( $(compgen -W ")" << MEMORY_SUBS << R"(" -- "$cur") );;
        sp)       COMPREPLY=( $(compgen -W ")" << SP_SUBS << R"(" -- "$cur") );;
        memoir)   COMPREPLY=( $(compgen -W ")" << MEMOIR_SUBS << R"(" -- "$cur") );;
        template) COMPREPLY=( $(compgen -W ")" << TEMPLATE_SUBS << R"(" -- "$cur") );;
        wiki)     COMPREPLY=( $(compgen -W ")" << WIKI_SUBS << R"(" -- "$cur") );;
    esac
}
complete -F _icmg_complete icmg
)";
    }

    void emitZsh() {
        std::cout <<
R"(#compdef icmg
# icmg zsh completion — auto-generated.
_icmg() {
    local -a top
    top=()" << TOP_CMDS << R"()
    if (( CURRENT == 2 )); then
        _describe 'icmg command' top
        return
    fi
    case "${words[2]}" in
        graph)    _values 'graph subcommand' )" << GRAPH_SUBS << R"( ;;
        memory)   _values 'memory subcommand' )" << MEMORY_SUBS << R"( ;;
        sp)       _values 'sp subcommand' )" << SP_SUBS << R"( ;;
        memoir)   _values 'memoir subcommand' )" << MEMOIR_SUBS << R"( ;;
        template) _values 'template subcommand' )" << TEMPLATE_SUBS << R"( ;;
        wiki)     _values 'wiki subcommand' )" << WIKI_SUBS << R"( ;;
    esac
}
_icmg "$@"
)";
    }

    void emitPwsh() {
        std::cout <<
R"(# icmg PowerShell completion — auto-generated.
$icmgTop = @()" << "'" << replaceSpacesWithCommaQuote(TOP_CMDS) << R"(')
Register-ArgumentCompleter -Native -CommandName icmg -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    $tokens = $commandAst.CommandElements
    if ($tokens.Count -le 2) {
        $icmgTop | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
            [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)
        }
    }
}
)";
    }

    static std::string replaceSpacesWithCommaQuote(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == ' ') out += "','";
            else out.push_back(s[i]);
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("completions", CompletionsCommand);

} // namespace icmg::cli
