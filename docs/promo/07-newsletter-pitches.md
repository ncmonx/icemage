# Newsletter / blog outreach — 3 pitches, copy/paste

Send these 1 week after a successful HN / Reddit post (newsletters love things already trending). If both flop, send anyway — newsletters break a fraction independent of HN.

---

## Outlet 1: Latent Space (latent.space)

**Audience:** 80K+ AI engineers, deeply technical, value receipts > marketing.

**Submission channels:**
- Tweet @swyx with a brief pitch + repo link
- Email submission via the form on https://www.latent.space/
- Their Discord #show-and-tell channel

**Pitch email/DM (≤140 chars for tweet, ≤300 words for email):**

```
Subject: Open-source CLI cutting Claude Code costs 70-90% — full code-tour + receipts

Hi swyx,

Solo-developer, 1.5 years in. Built and just shipped v1.1.1 of Icemage —
a single-binary CLI that sits in front of Claude Code (or any MCP-aware
agent) and compounds 7 token-saving layers to 85-95% per turn.

Why I think it fits Latent Space:

1. Receipts, not marketing. `icmg savings` shows per-project, per-day,
   per-tool tokens saved. JSON output for billing dashboards.
2. Local-first architecture — SQLite WAL + ONNX MiniLM embedder + tree-
   sitter parser. Zero cloud dep. 71/71 ctest.
3. Open MCP server — works for Claude Code, Cursor, Cline, Continue,
   and any future MCP client.
4. Real engineering content: why I dropped HNSW for an in-memory embed
   cache + BM25 prefilter, what the hook latency budget actually is,
   why I picked C++ over Rust for cold-start.

Happy to write a guest post or just share the repo for a "Show" segment.

Apache-2.0. https://github.com/ncmonx/icm-graph

Solo, no marketing budget. The point isn't to sell — the point is to
get the architecture critiqued by people who'd notice if it were wrong.

Thanks for considering.
— [Your name]
```

---

## Outlet 2: TLDR AI (tldr.tech/ai)

**Audience:** 500K+ subscribers, summary-driven readers, more mainstream.

**Submission:**
- https://tldr.tech/ has a "Submit a story" form
- Or email dan@tldr.tech with the headline + 2-sentence summary

**Pitch (very short — TLDR format):**

```
Headline (8 words max):
Open-source CLI cuts Claude Code costs 85%

Summary (2 sentences, ~40 words):
Icemage is a 17MB single binary that packs context bundles, filters
subprocess output, and tracks token savings for any AI coding agent.
Shipped 71/71 ctest on Windows + Linux. Apache-2.0, no account, runs
locally.

Link: https://github.com/ncmonx/icm-graph
```

TLDR uses a 1-paragraph format. Don't write 500 words.

---

## Outlet 3: Hacker Newsletter (hackernewsletter.com)

**Audience:** 60K+ subscribers, curated weekly digest of best HN content.

**Submission:**
- They scrape HN for posts that hit front page
- **Indirect strategy**: get to HN front page first (use 01-show-hn.md)
- Then they include you in the next weekend's digest automatically

No direct pitch needed — earn it via HN.

---

## Outlet 4 (bonus): The Pragmatic Engineer (Gergely Orosz)

**Audience:** 800K+ subscribers, deep dev focus.

**Submission:**
- Reply to one of Gergely's tweets with a polite pitch
- Or DM if he's open
- Subject angle: "Solo developer shipping engineering tools" is a recurring theme he covers

**Pitch tweet/DM (use only if relevant moment arises):**

```
Hi Gergely — saw your post on dev productivity tools / AI cost reality.

I'm a solo dev who just shipped v1.1.1 of an open-source CLI (icmg) that
cuts Claude Code token costs 70-90%. 18 months of nights/weekends.
Single binary, 71/71 ctest, no account.

Would love to write a guest post on "what 1.5 years of receipts-driven
token engineering taught me" — happy to send a draft if interested.

Apache-2.0. github.com/ncmonx/icm-graph
```

---

## Outlet 5 (Indonesia-local): teknologi.id / dailysocial.id / lokal tech blogs

Karena user Indonesian — coverage di outlet lokal masih kosong dan
relevan untuk awareness lokal.

**Pitch lokal (Bahasa Indonesia):**

```
Subject: Open-source CLI buatan dev Indonesia hemat biaya AI coding 85%

Halo redaksi,

Saya developer Indonesia, baru rilis v1.1.1 Icemage — CLI open-source
yang memotong biaya Claude Code / Cursor / AI coding agent lainnya
70-90% lewat 7 lapisan optimasi token. Single binary, 17 MB,
Apache-2.0. Sudah 71/71 tes lulus di Windows + Linux.

Angle yang menarik untuk pembaca lokal:
- Built by solo Indonesian dev, ~18 bulan after-hours
- Tanpa funding, tanpa investor, tanpa cloud lock-in
- Real numbers from daily usage, bukan promotional
- Useful untuk developer Indonesia yang pakai AI tools dengan budget
  terbatas

Repo: https://github.com/ncmonx/icm-graph
Latest release: v1.1.1

Saya available untuk wawancara / Q&A. Bisa share screenshot dashboard
penghematan token harian + comparison before/after kalau dibutuhkan.

Salam,
— [Nama]
```

---

## Outreach cadence

- **Week 1**: Show HN + Reddit r/ClaudeAI + Twitter thread.
- **Week 2**: Reddit r/LocalLLaMA + Twitter retweet/quote-tweet engagement.
- **Week 3**: Latent Space + TLDR AI pitch.
- **Week 4**: Lokal Indonesia outlet pitch + dev.to article published.
- **Month 2**: Awesome-list PRs (drip traffic).
- **Month 3+**: YouTube/TikTok 60s clip + LinkedIn long-form post.

Don't burn all channels in week 1. Spread across 3 months — sustained drip > single spike.

---

## Don't pitch to:

- ❌ Anyone who charges to feature your project. Pay-for-coverage = no audience trust.
- ❌ Generic "tech listicle" SEO farms. Zero conversion.
- ❌ Hacker News a second time within 30 days (HN penalizes reposts).
- ❌ LinkedIn DMs to strangers (most ignore; some report as spam).

---

## Track what's working

Free tools:
- **GitHub stars graph**: `https://api.star-history.com/svg?repos=ncmonx/icm-graph`
- **GoatCounter (free, <100K hits/mo)**: drop a 1px tracker on the README to count clicks per channel.
- **Plausible Analytics**: paid but cheap; better than GA for OSS.

Tag your URLs with UTM:
- `?utm_source=hn`
- `?utm_source=reddit_claude`
- `?utm_source=twitter`
- `?utm_source=devto`

After 30 days you'll know which channel converts. Double down on the top-2.
