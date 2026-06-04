#pragma once
// Pure predicate for the Windows "drive not found" (B:/) dialog signature.
// Extracted from popup_killer_cmd.cpp so it can be unit-tested without Win32.
// Three heuristics (any-of), gated on the system-dialog class #32770:
//   1/2. Short bare drive-letter title: "X:", "X:/", "X:\" (X in A-Z).
//   3.   Body text mentions a missing drive (drive/specified/tidak dapat menemukan)
//        -- covers dialogs whose CAPTION is empty or non-bare while the body says it.
#include <string>

namespace icmg::cli {

inline bool driveDialogMatch(const std::string& title,
                             const std::string& className,
                             bool bodyHasDriveText) {
    if (className != "#32770") return false;          // must be a Win32 system dialog
    // Heuristic 1/2: short bare drive-letter caption.
    if (title.size() >= 2 && title.size() <= 4) {
        char c = title[0];
        if (c >= 'A' && c <= 'Z' && title[1] == ':') return true;
    }
    // Heuristic 3: body text reveals it (caption may be empty/generic).
    if (bodyHasDriveText) return true;
    return false;
}

}  // namespace icmg::cli
