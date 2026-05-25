# 60-second demo video — reuse on YouTube Shorts / TikTok / X / LinkedIn

**Goal:** show, not tell. Numbers on screen do the persuading.

**Equipment:**
- OBS Studio (free) or Loom for capture
- Terminal at 16:9 aspect ratio, large readable font (≥18pt), dark theme
- Anything that records system audio if you want to add narration

**Length:** 55–60 seconds (Shorts/TikTok hard cap at 60).

**Aspect ratios to export:**
- 9:16 vertical (Shorts, TikTok, Reels) — primary
- 16:9 horizontal (YouTube main, X embed, LinkedIn)
- 1:1 square (LinkedIn, X fallback)

---

## Storyboard (frame-by-frame)

### 0:00–0:03 — Hook (text overlay only, no voice yet)

**Visual:** Black screen. Big white text:

> **My Claude Code bill last month: $340**

Cut.

> **This month: $48**

Hold 1 second.

> **Here's what changed.**

---

### 0:03–0:10 — Show the problem

**Visual:** Terminal. Run a Claude Code session:

```
$ claude
> read src/api/auth.ts
[1245 lines / 32,000 tokens used]
```

**Overlay:** red box around `32,000 tokens` with arrow → "for ONE file"

---

### 0:10–0:18 — Install icmg (real fast)

**Visual:** Split screen.

Left side: `curl -L .../icmg-1.1.1-win-x64.zip -o icmg.zip && unzip ...`
Right side: text overlay "1 binary. 17 MB. No account."

Cut to:
```
$ icmg init
+ .claude/settings.local.json (hooks installed)
+ icmg service: logon-trigger installed
Ready.
```

---

### 0:18–0:40 — The "after" — same task, dramatically less

**Visual:** Terminal again, same project, same task:

```
$ icmg pack "fix auth refresh bug"
[4096 byte context bundle ready]

$ claude
> /pack
[bundle injected — 4 KB / ~1000 tokens used]
```

**Overlay:** `32,000 → 1,000 tokens (-97%)`

Cut to:
```
$ icmg savings
project           tokens_saved   cost_saved
this-project      2,140,883      $32.11
TOTAL THIS MONTH  5,001,326      $75.02
```

Overlay grows on $75.02: pulse, slight zoom.

---

### 0:40–0:50 — What's inside (rapid fire, no narration, captions only)

**Visual:** quick cuts (1–2 seconds each) of:

1. `icmg recall "auth refresh"` → 3 past decisions printed instantly
2. `icmg run npm test` → 40 KB of output → 200 bytes shown
3. `icmg compress < schema.sql` → glossary view
4. `icmg fetch https://docs.anthropic.com/...` → cached reduce output

**Caption overlay (sticky):**
> 7 layers. They stack. 85–95% off per turn.

---

### 0:50–0:57 — CTA

**Visual:** static end card:

```
   I C E M A G E

   github.com/ncmonx/icm-graph

   Open source. Apache-2.0.
   Win + Linux. 71/71 tests.

   ⭐ if it saves you anything.
```

Small bottom-right: `ko-fi.com/ncmonx` (visible but not pushed).

---

### 0:57–1:00 — Outro tone (optional, audio only if you narrate)

Quick voice: "Built by one developer. Stays open." Fade out.

---

## Audio choices

**Option A — no narration, captions only.** Add lo-fi or synthwave royalty-free track. TikTok/Shorts often play muted; captions carry the message.

**Option B — voiceover.** Stick to ≤80 words total. Sample script:

```
[0:03] If your Claude Code bill keeps creeping up — same here.
[0:10] Most of those tokens are noise. Files the model didn't need to re-read.
[0:18] Icemage is one binary that fixes this.
[0:25] It builds a 4 kilobyte context bundle from your project graph and
       memory — instead of dumping whole files into the prompt.
[0:40] Seven layers stack to 85–95% off per turn.
[0:50] Open source. No account. Link in description.
```

---

## Distribution

| Platform | Format | Caption |
|---|---|---|
| YouTube Shorts | 9:16, ≤60s | "I cut my Claude Code bill 85% with this open-source CLI 🔥 #ClaudeCode #AI" |
| TikTok | 9:16, ≤60s | Same. Add: "#devtok #AItools #buildinpublic" |
| X / Twitter | 16:9 or 9:16, ≤2:20 hard cap | "Quick demo of icmg — open-source token-saver for Claude Code" + GitHub link |
| LinkedIn | 1:1 or 16:9 | More professional caption — "Solo OSS project, looking for feedback. Cuts Claude Code costs 70–90% via context engineering. Repo in comments." |
| Reddit (r/ClaudeAI) | Upload directly as Reddit video | Pin in your post |

**Reuse:** the same exported file works on 4 of the 5 platforms. Re-encode only for vertical Shorts/TikTok.

---

## Tools you'll need (all free)

- **OBS Studio** — screen capture
- **DaVinci Resolve (free)** or **CapCut (free)** — quick cuts + captions
- **Pixabay / YouTube Audio Library** — royalty-free music
- **Asciinema** — if you want a clean terminal recording you can embed as text-not-video on the GitHub README

Time investment: ~2 hours total once. Pays back for months across all 5 platforms.
