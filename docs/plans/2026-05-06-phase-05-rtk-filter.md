# Phase 05: RTK Filter + Runner

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Smart command runner dengan output filtering berdasarkan command type, auto-record command frequency, dan token-efficient output.
**Architecture:** Runner capture stdout+stderr. Detector classify command type. Filter strategy terapkan sesuai type. Registry-based filter lookup.
**Tech Stack:** C++17, popen/CreateProcess (cross-platform), regex
**Assumptions:** Phase 01 selesai. Shell tersedia di sistem.

---

### Task 1: Command type detector

**Files:**
- Create: `src/rtk/detector.hpp`
- Create: `src/rtk/detector.cpp`

```cpp
enum class CmdType {
    GitLog,         // git log, git diff, git show, git status
    Build,          // cargo build, cmake, make, dotnet build, npm run build
    Test,           // cargo test, npm test, pytest, dotnet test
    Search,         // grep, rg, find (output-heavy)
    Docker,         // docker build, docker logs
    Default         // semua yang tidak dikenal
};

class Detector {
public:
    CmdType detect(const std::string& command) const;
private:
    struct Pattern { std::string prefix; CmdType type; };
    std::vector<Pattern> patterns_;
};
```

Pattern matching: prefix-based + keyword dalam command string.

---

### Task 2: Base filter interface

**Files:**
- Create: `src/rtk/base_filter.hpp`

```cpp
struct FilterResult {
    std::string output;
    int original_lines;
    int filtered_lines;
    bool was_truncated;
};

class BaseFilter {
public:
    virtual ~BaseFilter() = default;
    virtual FilterResult filter(const std::string& raw_output,
                                const std::string& command) = 0;
    virtual std::string name() const = 0;
};
```

---

### Task 3: Filter implementations

**Files:**
- Create: `src/rtk/filters/git_filter.cpp`
- Create: `src/rtk/filters/build_filter.cpp`
- Create: `src/rtk/filters/test_filter.cpp`
- Create: `src/rtk/filters/search_filter.cpp`
- Create: `src/rtk/filters/default_filter.cpp`

**GitFilter:**
- `git status`: keep as-is (biasanya pendek)
- `git log`: keep max 30 entries, trim hash ke 8 chars
- `git diff`: keep changed lines (+/-) + 3 context lines, strip binary diff
- `git show`: kombinasi log + diff rules
- Registration: `ICMG_REGISTER_FILTER("git", GitFilter);`

**BuildFilter:**
- Hapus: progress bars, download progress, verbose linking info
- Keep: lines contain "error", "warning", "error[", "warning[", "FAILED", "LINK"
- Keep: summary line terakhir (X errors, Y warnings)
- Registration: `ICMG_REGISTER_FILTER("build", BuildFilter);`

**TestFilter:**
- Keep: FAILED test cases + panic messages + assertion details
- Keep: summary baris terakhir (test result: X passed, Y failed)
- Hapus: "running X tests", individual "ok" lines
- Registration: `ICMG_REGISTER_FILTER("test", TestFilter);`

**SearchFilter:**
- Group results by file
- Max 200 lines total
- Format: `file.cpp:42: matching line content`
- Jika > 200 lines: `... N more matches in M files`

**DefaultFilter:**
- Keep first 50 lines + last 20 lines
- Jika > 70 lines: insert `\n... (N lines omitted) ...\n` di tengah

---

### Task 4: Command runner (cross-platform)

**Files:**
- Create: `src/rtk/runner.hpp`
- Create: `src/rtk/runner.cpp`

```cpp
struct RunResult {
    std::string stdout_raw;
    std::string stderr_raw;
    int exit_code;
    int64_t duration_ms;
};

class Runner {
public:
    RunResult run(const std::string& command, bool merge_stderr = true);
private:
    // Windows: _popen / ReadFile via HANDLE
    // Unix: popen / read
};
```

---

### Task 5: RTK orchestrator + auto-record

**Files:**
- Create: `src/rtk/rtk.hpp`
- Create: `src/rtk/rtk.cpp`

```cpp
class RTK {
public:
    explicit RTK(core::Db& db);

    // Run + filter + print + record
    int runFiltered(const std::string& command, bool raw = false, bool json = false);

    // Suggest commands by score (freq x recency)
    std::vector<std::string> suggest(const std::string& prefix = "", int limit = 10);

private:
    void recordCommand(const std::string& cmd, int output_lines);
    BaseFilter* getFilter(CmdType type) const;
    core::Db& db_;
};
```

Auto-record: setiap `run` → bump frequency di tabel commands.

---

### Task 6: CLI commands: run + cmd suggest + cmd record

**Files:**
- Create: `src/cli/commands/run_cmd.cpp`
- Create: `src/cli/commands/cmd_cmd.cpp`

```
icmg run <command...>              # run + smart filter
icmg run --raw <command...>        # no filter, raw output
icmg run --json <command...>       # JSON output with metadata
icmg cmd suggest [<prefix>]        # suggest by score
icmg cmd suggest git --limit 5
icmg cmd record "git log --oneline -20"   # manual record
icmg cmd list [--limit N]
```

**run --json output:**
```json
{
  "command": "git log --oneline -10",
  "exit_code": 0,
  "duration_ms": 45,
  "original_lines": 10,
  "filtered_lines": 10,
  "was_truncated": false,
  "output": "abc1234 feat: add scorer\n..."
}
```

**cmd suggest output:**
```
Score  Frequency  Last Used   Command
 98.2      47x    5 min ago   git log --oneline -20
 87.1      31x    1 hour ago  cargo build --release
 71.3      18x    3 hours ago rg "TODO" src/
```

---

### Task 7: Final verify + commit

**Step 1:**
```bash
cmake --build build
./build/icmg run git log --oneline -5
./build/icmg run --raw git log --oneline -5
./build/icmg run --json git log --oneline -5
./build/icmg cmd suggest git
./build/icmg cmd suggest
```
Expected: filtered output lebih ringkas dari raw, JSON valid, suggest terurut by score.

**Step 2: Commit**
```bash
git add src/rtk/ src/cli/commands/run_cmd.cpp src/cli/commands/cmd_cmd.cpp
git commit -m "feat: phase-05 RTK smart filter + runner + cmd history"
```

---

## Amendments from Security & Architecture Review

### CRITICAL Fixes

**A1 — TIDAK BOLEH popen(string) dengan user input**
Runner.cpp WAJIB pakai argv array:
```cpp
// SALAH:
FILE* f = popen(("git " + userCommand).c_str(), "r");

// BENAR (Unix):
ExecResult safeExec(const std::vector<std::string>& argv) {
    int pipefd[2];
    pipe(pipefd);
    pid_t pid = fork();
    if (pid == 0) {
        // child
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        execvp(argv[0].c_str(), /* convert to char** */);
    }
    // parent: read pipefd[0]
}

// BENAR (Windows):
// Bangun lpCommandLine dari joined argv dengan proper quoting
// Gunakan CreateProcess() dengan NULL lpApplicationName
```

Parser argv dari command string user: split by whitespace dengan respect quotes `"..."`.

### HIGH Additions

**A2 — Streaming filter mode**
```cpp
class StreamingFilter {
public:
    // Dipanggil per-line saat output datang (bukan setelah selesai)
    virtual bool shouldKeep(const std::string& line) = 0;
    virtual std::string transform(const std::string& line) { return line; }
};
```
```
icmg run --stream cargo build   # filter real-time, tampilkan saat datang
icmg run cargo build            # default: buffer + filter (untuk non-TTY)
```
Auto-detect: jika stdout adalah TTY → streaming mode. Jika pipe/redirect → buffer mode.

**A3 — Custom filter rules via config**
`~/.icmg/filters.json` atau `.icmg/filters.json` (project-level override):
```json
{
  "rules": [
    {
      "match_command": "gradle.*",
      "keep_patterns": ["FAILED", "BUILD", "ERROR", "error:"],
      "truncate_strategy": "errors_only"
    },
    {
      "match_command": "docker logs",
      "keep_patterns": ["ERROR", "WARN", "Exception"],
      "tail_lines": 50
    }
  ]
}
```
Load di Config engine. FilterRegistry cek custom rules sebelum built-in filters.

**A4 — Repeated output deduplication**
```cpp
// Hash filtered output. Jika sama dengan N menit lalu:
auto hash = sha256(filteredOutput);
if (hash == lastRunHash(command) && lastRunAge(command) < 10min) {
    std::cout << "[Same output as " << lastRunAgo << " ago. --force to show]\n";
    return lastExitCode(command);
}
```

**A5 — npm/yarn/pip/gem filter**
Tambahkan `PackageManagerFilter`:
- Match: `npm install`, `yarn`, `pip install`, `gem install`
- Keep: warnings, errors, final summary (`added N packages`, `Successfully installed`)
- Hapus: individual download progress, extract progress

Registration: `ICMG_REGISTER_FILTER("npm", PackageManagerFilter);`

**A6 — Token budget tracking**
```sql
ALTER TABLE commands ADD COLUMN total_original_lines INTEGER DEFAULT 0;
ALTER TABLE commands ADD COLUMN total_filtered_lines INTEGER DEFAULT 0;
```
Update setiap run. `icmg rtk stats` output:
```
Total runs: 847
Avg reduction: 73%
Est. tokens saved: ~1.2M
Top savers:
  git log    47x  avg 81% reduction
  cargo build 31x avg 89% reduction
```

### MEDIUM Additions

**A7 — Dry-run mode**
```
icmg run --dry-run cargo build
```
Output:
```
Command: cargo build
Detected type: Build
Filter: BuildFilter (errors+warnings only)
Would show: errors, warnings, summary line
Would hide: progress bars, download lines, verbose linking
[To execute: icmg run cargo build]
```

**A8 — stdin support**
```
cat output.log | icmg run --filter build -   # filter stdin as build output
icmg sp add sp_Foo -                          # read SQL dari stdin
```
Detect `-` sebagai path argument = read from stdin.

**A9 — Hard output size limit**
Semua filter: max 500 lines output regardless of strategy. Jika melebihi:
```
... (output truncated at 500 lines, N lines omitted) ...
```
