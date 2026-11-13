// TDD (2026-11-13, issue #281): `icmg update --apply` refreshes project hooks
// via `init --install-hooks --force`, and init used to overwrite every hook
// file unconditionally -- a user's local edits (e.g. an rg fallback added to
// .claude/grep-hook.js) were silently reset on every upgrade.
// Spec: init records the hash of every hook it writes in a manifest. On the
// next refresh a file whose content still matches the recorded hash is
// pristine (safe to upgrade); a file that differs was edited by the user and
// is kept, with the new template dropped next to it as <file>.icmg-new.
// Legacy files (no manifest record yet) that differ from the template are
// backed up to <file>.user-bak before being replaced.
#include "../test_main.hpp"
#include "../../src/cli/hook_preserve.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using icmg::cli::HookAction;
using icmg::cli::decideHookAction;
using icmg::cli::hookHash;

TEST("hook_preserve: hash is stable and content-sensitive") {
    ASSERT_EQ(hookHash("abc"), hookHash("abc"));
    ASSERT_TRUE(hookHash("abc") != hookHash("abd"));
    ASSERT_EQ((int)hookHash("").size(), 16);
}

TEST("hook_preserve: missing file -> create") {
    ASSERT_TRUE(decideHookAction(false, "", "T", "", false) == HookAction::Create);
}

TEST("hook_preserve: identical to template -> unchanged") {
    ASSERT_TRUE(decideHookAction(true, "T", "T", "", false) == HookAction::Unchanged);
}

TEST("hook_preserve: pristine older template -> upgrade") {
    std::string old_tmpl = "OLD";
    ASSERT_TRUE(decideHookAction(true, old_tmpl, "NEW", hookHash(old_tmpl), false)
                == HookAction::Upgrade);
}

TEST("hook_preserve: user-edited hook is kept (the #281 regression)") {
    std::string written = "OLD";
    std::string edited  = "OLD + viaRg fallback";
    ASSERT_TRUE(decideHookAction(true, edited, "NEW", hookHash(written), false)
                == HookAction::KeepUser);
}

TEST("hook_preserve: --overwrite-hooks forces backup+write of user edits") {
    ASSERT_TRUE(decideHookAction(true, "EDITED", "NEW", hookHash("OLD"), true)
                == HookAction::BackupAndWrite);
}

TEST("hook_preserve: legacy file without record that differs -> backup+write") {
    ASSERT_TRUE(decideHookAction(true, "LEGACY", "NEW", "", false)
                == HookAction::BackupAndWrite);
}

TEST("hook_preserve: manifest round-trips through the .claude dir") {
    fs::path dir = fs::temp_directory_path() / "icmg_hook_preserve_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::map<std::string, std::string> m;
    m["hooks/icmg-bash-rewrite.sh"] = hookHash("a");
    m["grep-hook.js"] = hookHash("b");
    ASSERT_TRUE(icmg::cli::saveHookManifest(dir, m));
    auto back = icmg::cli::loadHookManifest(dir);
    ASSERT_EQ((int)back.size(), 2);
    ASSERT_EQ(back["grep-hook.js"], hookHash("b"));
    fs::remove_all(dir, ec);
}

TEST("hook_preserve: corrupt or missing manifest loads empty") {
    fs::path dir = fs::temp_directory_path() / "icmg_hook_preserve_bad";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ASSERT_EQ((int)icmg::cli::loadHookManifest(dir).size(), 0);
    { std::ofstream(dir / icmg::cli::kHookManifestName) << "{not json"; }
    ASSERT_EQ((int)icmg::cli::loadHookManifest(dir).size(), 0);
    fs::remove_all(dir, ec);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
