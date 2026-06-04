// Pure predicate for the B:/ "drive not found" dialog -- 3 heuristics, class-gated.
#include "../test_main.hpp"
#include "../../src/cli/drive_dialog_match.hpp"
using namespace icmg::cli;

TEST("driveDialog: bare 'B:/' caption + #32770 matches (heuristic 2)") {
    ASSERT_TRUE(driveDialogMatch("B:/", "#32770", false));
}

TEST("driveDialog: bare 'B:' caption + #32770 matches (heuristic 1)") {
    ASSERT_TRUE(driveDialogMatch("B:", "#32770", false));
}

TEST("driveDialog: empty/generic caption + body drive-text matches (heuristic 3)") {
    ASSERT_TRUE(driveDialogMatch("", "#32770", true));
    ASSERT_TRUE(driveDialogMatch("Error", "#32770", true));
}

TEST("driveDialog: non-system class never matches") {
    ASSERT_TRUE(!driveDialogMatch("B:/", "Button", true));
}

TEST("driveDialog: ordinary system dialog (no drive title, no drive body) no match") {
    ASSERT_TRUE(!driveDialogMatch("Save changes?", "#32770", false));
}

TEST("driveDialog: long non-drive caption alone no match") {
    ASSERT_TRUE(!driveDialogMatch("Some long window title here", "#32770", false));
}
