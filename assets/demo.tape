# icmg demo — declarative terminal GIF recording (charmbracelet/vhs).
#
# Regenerate the hero GIF:
#   1. Install vhs:  go install github.com/charmbracelet/vhs@latest   (or: brew install vhs)
#   2. Make sure `icmg` is on PATH and you are inside a real project (e.g. icemage itself).
#   3. Run:  vhs assets/demo.tape
#   4. Commit the produced assets/demo.gif and uncomment the <img> in README.md hero.
#
# Keep it ~30s. Show the three things that sell icmg: savings, one-shot find, slim context.

Output assets/demo.gif

Set Shell "bash"
Set FontSize 18
Set Width 1200
Set Height 640
Set Padding 18
Set Theme "Dracula"
Set TypingSpeed 45ms

Hide
Type "clear" Enter
Show

# 1. How much have I saved?
Type "icmg savings" Sleep 500ms Enter
Sleep 2.5s

# 2. One-shot search — no multi-turn grep ping-pong.
Type "icmg find 'where is the context budget computed'" Sleep 500ms Enter
Sleep 3s

# 3. Read a big file as a slim, relevant slice instead of 30K tokens.
Type "icmg context src/cli/context_budget.hpp" Sleep 500ms Enter
Sleep 3s

# 4. Savings over time, as an in-terminal chart.
Type "icmg savings --ascii" Sleep 500ms Enter
Sleep 2.5s

Sleep 1s
