# Phase 14: Integration Testing + Bug Fix

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** End-to-end integration tests untuk semua fitur, bug fixes dari testing, dan CLAUDE.md setup untuk project ini.
**Architecture:** Shell-based integration tests (bash scripts). Tidak perlu unit test framework — test via CLI output.
**Tech Stack:** C++17, Bash (test scripts)
**Assumptions:** Phase 01-13 selesai dan build sukses.

---

### Task 1: Integration test script: ICM

**Files:**
- Create: `tests/test_icm.sh`

```bash
#!/bin/bash
set -e
ICMG="./build/icmg"

# Test store + recall
$ICMG store test-topic "hello world testing" --importance high --kw test,hello
RESULT=$($ICMG recall "hello" --json)
echo $RESULT | grep -q "hello world" && echo "PASS: recall basic" || echo "FAIL: recall basic"

# Test scoring (high importance harus rank lebih tinggi)
$ICMG store low-topic "hello world" --importance low
FIRST=$($ICMG recall "hello" --limit 1 --json | grep topic)
echo $FIRST | grep -q "test-topic" && echo "PASS: scoring importance" || echo "FAIL: scoring importance"

# Test forget
$ICMG store forget-test "to be forgotten"
ID=$($ICMG recall "forgotten" --json | grep id | head -1 | grep -o '[0-9]*')
$ICMG forget $ID
COUNT=$($ICMG recall "forgotten" --json | grep -c "forgotten")
[ "$COUNT" -eq 0 ] && echo "PASS: forget" || echo "FAIL: forget"
```

---

### Task 2: Integration test script: Graph

**Files:**
- Create: `tests/test_graph.sh`

```bash
#!/bin/bash
set -e
ICMG="./build/icmg"

# Scan src/ dan verify nodes terbuat
$ICMG graph scan src/ --depth 10
COUNT=$($ICMG graph list --json | grep -c "path")
[ "$COUNT" -gt 0 ] && echo "PASS: graph scan" || echo "FAIL: graph scan"

# Context tersedia setelah scan
CONTEXT=$($ICMG graph context src/main.cpp --json)
echo $CONTEXT | grep -q "path" && echo "PASS: graph context" || echo "FAIL: graph context"

# Related files
RELATED=$($ICMG graph related src/core/db.cpp --limit 3 --json)
echo $RELATED | grep -q "path" && echo "PASS: graph related" || echo "FAIL: graph related"
```

---

### Task 3: Integration test script: RTK

**Files:**
- Create: `tests/test_rtk.sh`

```bash
#!/bin/bash
set -e
ICMG="./build/icmg"

# Run command + verify filtered
RAW_LINES=$(git log --oneline -50 | wc -l)
FILTERED_LINES=$($ICMG run git log --oneline -50 | wc -l)
[ "$FILTERED_LINES" -le "$RAW_LINES" ] && echo "PASS: RTK filters output" || echo "FAIL: RTK no filtering"

# JSON output valid
JSON=$($ICMG run --json git log --oneline -5)
echo $JSON | grep -q "exit_code" && echo "PASS: RTK JSON output" || echo "FAIL: RTK JSON"

# Cmd suggest setelah beberapa runs
$ICMG run git log --oneline -3
$ICMG run git log --oneline -3
SUGGEST=$($ICMG cmd suggest git --json)
echo $SUGGEST | grep -q "git log" && echo "PASS: cmd suggest" || echo "FAIL: cmd suggest"
```

---

### Task 4: Integration test script: Rules + Data + Abbr + SP

**Files:**
- Create: `tests/test_features.sh`

Test masing-masing fitur:
- rule add + list + apply → verify inheritance
- data add + show + search → verify BM25
- abbr learn + expand → verify auto-expand di recall
- sp add + show + deps → verify SQL parsing

---

### Task 5: Integration test script: MCP

**Files:**
- Create: `tests/test_mcp.sh`

```bash
#!/bin/bash
ICMG="./build/icmg"

test_mcp() {
    local method=$1
    local params=$2
    echo "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"$method\",\"params\":$params}" \
        | $ICMG --mcp-server | grep -v "^$"
}

# Initialize
INIT=$(test_mcp "initialize" "{}")
echo $INIT | grep -q "serverInfo" && echo "PASS: MCP initialize" || echo "FAIL: MCP initialize"

# List tools
TOOLS=$(test_mcp "tools/list" "{}")
echo $TOOLS | grep -q "icmg_recall" && echo "PASS: MCP list tools" || echo "FAIL: MCP list tools"

# Call recall tool
RECALL=$(test_mcp "tools/call" "{\"name\":\"icmg_recall\",\"arguments\":{\"query\":\"hello\"}}")
echo $RECALL | grep -q "result" && echo "PASS: MCP recall" || echo "FAIL: MCP recall"
```

---

### Task 6: Master test runner

**Files:**
- Create: `tests/run_all.sh`

```bash
#!/bin/bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -- -j4
bash tests/test_icm.sh
bash tests/test_graph.sh
bash tests/test_rtk.sh
bash tests/test_features.sh
bash tests/test_mcp.sh
echo "All integration tests completed."
```

---

### Task 7: CLAUDE.md setup

**Files:**
- Create: `CLAUDE.md`

CLAUDE.md untuk project icmg sendiri:
- Instruksi: pakai RTK untuk semua build commands (`icmg run cmake --build build`)
- MCP: icmg --mcp-server untuk recall/store/graph
- Rules: coding conventions untuk C++ di project ini
- Graph: `icmg graph scan src/` setelah tambah file baru

---

### Task 8: Performance check

**Step 1: Benchmark scan besar**
```bash
time ./build/icmg graph scan src/ --depth 20
```
Expected: < 5 detik untuk ~100 files.

**Step 2: Benchmark recall**
```bash
time ./build/icmg recall "core database" --limit 10
```
Expected: < 100ms.

**Step 3: Memory usage**
Tidak boleh > 100MB untuk project 500 files.

---

### Task 9: Final commit + tag

```bash
bash tests/run_all.sh
git add tests/ CLAUDE.md .claude/mcp.json
git commit -m "feat: phase-14 integration tests + CLAUDE.md + verified build"
git tag v1.0.0
```

---

## Amendments from Security & Architecture Review

### New Tasks Added

**Task 9a: Security integration tests**
**Files:**
- Create: `tests/test_security.sh`

```bash
#!/bin/bash
ICMG="./build/icmg"

# Test: path traversal di graph context tidak bekerja
RESULT=$($ICMG graph context "../../../../etc/passwd" 2>&1 || true)
echo $RESULT | grep -q "outside project root" && echo "PASS: path traversal blocked" || echo "FAIL: path traversal not blocked"

# Test: command injection di run tidak bekerja
RESULT=$($ICMG run "echo hello; rm -f /tmp/icmg_injection_test" 2>&1 || true)
[ ! -f /tmp/icmg_injection_test ] && echo "PASS: command injection blocked" || echo "FAIL: injection succeeded"

# Test: import file > 100MB rejected
dd if=/dev/zero bs=1M count=101 > /tmp/huge.json 2>/dev/null
RESULT=$($ICMG import json /tmp/huge.json 2>&1 || true)
echo $RESULT | grep -q "too large" && echo "PASS: large file rejected" || echo "FAIL: large file not rejected"
rm /tmp/huge.json

# Test: DB file permissions adalah 0600
$ICMG store test-perm "permission test"
PERMS=$(stat -c %a .icmg/data.db 2>/dev/null || stat -f %Lp .icmg/data.db)
[ "$PERMS" = "600" ] && echo "PASS: DB file is 0600" || echo "FAIL: DB permissions = $PERMS"
```

**Task 9b: CI/CD exit code contracts**
Semua analytical commands return exit code 1 jika findings exceed threshold:
```bash
icmg graph cycles --fail-on-found    # exit 1 jika ada cycle
icmg graph orphans --fail-on-found   # exit 1 jika ada orphan
icmg sp lint --all --fail-on-error   # exit 1 jika ada lint error
icmg sp lint --all --fail-on-warn    # exit 1 jika ada warning
```
Document di README sebagai CI pipeline gates.

**Task 9c: Performance benchmarks**
```bash
# tests/test_performance.sh
time ./build/icmg graph scan src/ --depth 20
# Expected: < 5 detik untuk ~100 files

time ./build/icmg recall "core database" --limit 10
# Expected: < 100ms

time ./build/icmg graph impact src/core/db.cpp
# Expected: < 500ms untuk graph 500 nodes

# Memory usage check
./build/icmg graph scan . --depth 20 &
sleep 2
cat /proc/$!/status | grep VmRSS
# Expected: < 100MB RSS
```

**Task 9d: Fuzzing target untuk MCP JSON parser**
```bash
# Jika menggunakan AFL++ atau libFuzzer:
# Build dengan -fsanitize=address,fuzzer
# Target: McpServer JSON parsing entrypoint
# Expected: no crashes, no hangs pada arbitrary input
```
