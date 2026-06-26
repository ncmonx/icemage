---
description: ATURAN ABSOLUT build & test project icemage (C++/CMake/MSVC di Windows). Pakai SEBELUM build, test, atau menyentuh apa pun yang berhubungan dengan CMake/build.ps1/CMakeLists. Mencegah pelanggaran aturan ABSOLUT CLAUDE.md.
trigger: build test cmake build.ps1 ctest CMakeLists compile rebuild pwsh msvc
---

# icemage — Build & Test Rules (ABSOLUT)

> Sumber: CLAUDE.md "ATURAN MUTLAK & ABSOLUT" + memory #35906 (cmake dilarang) + #35735 (ctest hang).
> Langgar ini = buang waktu kak Cahyo + langgar kepercayaan. Patuh = default.

## 1. CARA BUILD (WAJIB — tidak ada pengecualian)

- Build **HANYA** lewat **`Build` tool** ATAU **`pwsh -File build.ps1`**.
- **DILARANG KERAS** `cmake --build ...` atau `cmake ...` langsung di Bash/TaskCreate.
  - Bukan "cuma target tertentu", bukan "cuma compile cepat". TIDAK ADA pengecualian.
- `build.ps1` dijalankan dengan **`pwsh` (PowerShell 7)**, BUKAN `powershell` (5.1).
- Build itu LAMA (>1-2 menit) → **WAJIB BACKGROUND**: pakai `Build` tool (sudah async) atau `TaskCreate`, JANGAN blocking Bash (nge-freeze sesi + kena cap timeout).

## 2. JANGAN baca-ulang log dengan rebuild

- Build log tersimpan: `%USERPROFILE%\.icmg\build-logs\msvc-build-latest.log`
- Lihat tanpa rebuild: `pwsh -File build.ps1 -ShowLog [-Lines N]`
- Atau pakai `Build` tool yang sudah balikin ringkasan terstruktur (ok/fail + error lines + warning count).
- JANGAN rebuild cuma buat lihat error lama.

## 3. IZIN sebelum sentuh setting build

- **DILARANG edit** `CMakeLists.txt`, `CMakePresets.json`, `build.ps1`, config, atau setting build apa pun **TANPA izin eksplisit kak Cahyo**. Tanya dulu, tunggu "ya".
- `build.ps1` komentar **WAJIB ASCII-only** (no box-draw/em-dash) supaya PS 5.1 + 7 dua-duanya parse.
- Menambah file **source** baru → TIDAK perlu edit CMakeLists (auto `GLOB_RECURSE` atas `src/*.cpp`).
- Menambah **test** baru → WAJIB manual `add_executable` + `target_link_libraries(... icmg_lib)` + `add_test` di CMakeLists.txt.

## 4. JANGAN hapus artifact build valid

- DILARANG hapus `build-msvc-full/third_party/`, nested `CMakeCache.txt`, atau artifact build valid.
- Reconfigure = hapus **top-level** `CMakeCache.txt` saja.

## 5. TESTING

- Full suite: `ctest --test-dir build --output-on-failure`
- Single: `ctest --test-dir build -R test_<name>` atau jalanin binary langsung `./build/test_<name>` (iterasi cepat).
- **ctest bisa HANG** di test yang spawn resident process (mis. llama-server). Root cause sudah di-fix (Process.cpp STARTUPINFOEX handle-list, commit 9817076), tapi kalau hang lagi: exclude dengan **`ctest -LE local-llm`** (label local-llm + TIMEOUT sudah dipasang).
- Semua test link ke **`icmg_lib`** (semua kecuali `main.cpp`).
- Tests mirror layout `src/` di bawah `tests/`.

## 6. TDD (wajib sejak 2026-05)

- Setiap command / behavior change baru → **failing test DULU** sebelum implementasi.
- Target v1.0: tiap cmd di `src/cli/commands/` punya minimal 1 ctest target.

## 7. ANTI-DUP REFLEX (sebelum bikin command baru)

- Sebelum bikin `*_cmd.cpp` / `ICMG_REGISTER_COMMAND` baru: jalankan `icmg suggest "<purpose>"` + `icmg map <nearest-cmd>`.
- Kalau ada match dekat → **EXTEND** (tambah flag/subcommand), JANGAN bikin command paralel.

## Checklist cepat sebelum build/test
1. Build? → `Build` tool atau `pwsh -File build.ps1` (BUKAN cmake langsung), background.
2. Mau lihat error lama? → `-ShowLog`, jangan rebuild.
3. Mau ubah CMake/build.ps1/config? → IZIN dulu.
4. ctest hang? → `ctest -LE local-llm`.
5. Command baru? → anti-dup check + failing test dulu.
