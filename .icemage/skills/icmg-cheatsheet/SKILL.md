---
description: Cheat-sheet command icmg yang BENER-BENER dipakai AI coding sehari-hari (dari 258 command, ~30 inti). Pakai saat ragu "command icmg apa buat X" -- biar fokus ke yang penting, bukan kebanjiran pilihan. Untuk yang ga ada di sini -> `icmg suggest "<intent>"`.
trigger: icmg command which cheatsheet mana command apa pakai recall graph context find suggest
---

# icmg Cheat-Sheet (AI daily-driver)

> 258 command itu sebagian besar maintenance/power-user. Ini ~30 yang dipakai
> tiap hari. Ragu? `icmg suggest "<apa yang mau dilakukan>"` (router precision
> udah di-fix: "who calls X" -> graph-callers, dst). Famili udah ke-namespace:
> `icmg graph <sub>` == `icmg graph-<sub>`.

## 1. Baca & cari (ganti Read/Grep/Glob native)
| Mau | Command |
|---|---|
| Baca file besar (graph+symbol+memory) | `icmg context <file>` |
| Cari simbol (fungsi/class, partial ok) | `icmg graph-symbol <Name>` |
| Cari file by intent (di mana X ditangani) | `icmg find "<intent>"` |
| Cari file by nama (fuzzy, cepat) | `icmg find --name <partial>` [`--open` `--recent`] |
| Cari teks (auto-filtered) | `icmg run grep ...` |
| Cari node graph by query | `icmg graph-search <query>` |
| Tanya graph natural-language | `icmg graph-query query "<NL>"` / `explain "<node>"` |

## 2. Navigasi dependency (code graph)
| Mau | Command |
|---|---|
| Siapa yang MANGGIL simbol | `icmg graph-callers <Name>` |
| Apa yang DIPANGGIL simbol | `icmg graph-callees <Name>` |
| File kena dampak kalau ubah X | `icmg graph-impact <file>` |
| Siapa rusak kalau X berubah (reverse) | `icmg graph-reverse-impact <Name> --depth 5` |
| Path terpendek antar 2 file | `icmg graph-path <from> <to>` |
| Tetangga 1-hop | `icmg graph-neighbors <file>` |
| Dep upstream bersama 2 file | `icmg graph-common <a> <b>` |
| Re-scan graph (SLOW -> background) | `icmg graph-update` |

## 3. Memory / otak (recall sebelum kerja, remember sesudah)
| Mau | Command |
|---|---|
| Recall keputusan/fix lampau | `icmg recall "<query>"` [`--semantic`] |
| Recall lintas-project | `icmg cross-recall "<query>"` |
| Simpan keputusan | `icmg store --topic decisions-<area> "<text>"` |
| Briefing awal sesi | `icmg wake-up` |
| Mulai task (bundle 4KB) | `icmg pack "<task>"` |
| Anti-pattern (gagal lampau) | `icmg fail recall "<task>"` / `icmg fail store ...` |
| Errored sebelumnya? | `icmg explain "<error>"` |
| Browse/hapus memory | `icmg memory list/show/forget/search` |

## 4. Kerja & verifikasi
| Mau | Command |
|---|---|
| Jalanin command noisy (filter 60-90%) | `icmg run <cmd>` |
| 2+ task independen (WAJIB) | `icmg parallel --task "..." --task "..."` |
| Git apa pun (filtered/gated) | `icmg git <subcmd>` |
| Diff besar | `icmg diff-summary --ref HEAD~5` |
| Catat verifikasi (audit) | `icmg verify --command "<cmd>"` |
| Fetch URL (cache+reduce) | `icmg fetch <url>` |

## 5. Meta (kalau lupa command)
| Mau | Command |
|---|---|
| Command apa buat X? | `icmg suggest "<intent>"` |
| Command tetangga / sekeluarga | `icmg map <cmd>` |
| Router exec | `icmg ask "<question>"` |

## Aturan refleks
- **icmg-first**: cek command icmg dulu sebelum native Read/Grep/Glob/Bash/WebFetch.
- **parallel-first**: 2+ task independen -> `icmg parallel`, jangan sekuensial.
- **anti-dup**: sebelum bikin command baru -> `icmg suggest "<purpose>"` + `icmg map <near>`; kalau ada match, EXTEND.
- **build/test**: lihat skill `icemage-build-test` (build via `pwsh build.ps1`, BUKAN cmake langsung).
- Ga ketemu di sini? 258 command totalnya -> `icmg suggest "<intent>"` selalu jadi jalan pertama.
