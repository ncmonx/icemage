# icmg Hook Templates

Drop-in hook scripts for **Claude Code** (and other PreToolUse/PostToolUse hosts).

Each hook takes Tool input/output JSON on stdin and returns a JSON response controlling tool behavior.

---

## Available Hooks

### `icmg-shrink-read.sh` (PreToolUse on `Read`)

Replaces large-file reads with `icmg summarize` output. Saves 80-90% on accidentally reading huge files.

**Install:** add to `.claude/settings.local.json`:

```json
{
  "hooks": {
    "PreToolUse": [{
      "matcher": "Read",
      "hooks": [{
        "type": "command",
        "command": "/path/to/examples/hooks/icmg-shrink-read.sh"
      }]
    }]
  }
}
```

**Threshold:** files >60KB by default (~1000+ lines). Override with `ICMG_SHRINK_THRESHOLD=100000`.

**Behavior (default, soft):** appends summary as `additionalContext` — Read still proceeds, agent gets summary alongside content. No confirm prompts.

**Strict mode (`ICMG_SHRINK_STRICT=1`):** denies Read entirely; agent must use `--offset/--limit` or `icmg context`.

**Exclude / include patterns:**
- `ICMG_SHRINK_EXCLUDE='\.cs$|src/legacy/'` — egrep regex; matching files bypass hook
- `ICMG_SHRINK_INCLUDE='\.(md|json)$'` — only check matching files

Example install for project that has many large `.cs` files you want full Read on:
```json
{
  "hooks": {
    "PreToolUse": [{
      "matcher": "Read",
      "hooks": [{
        "type": "command",
        "command": "ICMG_SHRINK_THRESHOLD=120000 ICMG_SHRINK_EXCLUDE='\\.cs$' /path/to/icmg-shrink-read.sh"
      }]
    }]
  }
}
```

---

### `icmg-cap-output.sh` (PostToolUse on `Bash`)

Caps stdout >8KB by spilling to `/tmp/icmg-spill-<hash>.txt` and returning head+tail to the model.

**Install:**

```json
{
  "hooks": {
    "PostToolUse": [{
      "matcher": "Bash",
      "hooks": [{
        "type": "command",
        "command": "/path/to/examples/hooks/icmg-cap-output.sh"
      }]
    }]
  }
}
```

**Threshold:** 8KB default. Override with `ICMG_CAP_BYTES=16384`.

**Behavior:** appends `additionalContext` with truncated view + spill path; the model sees a smaller payload but can `cat /tmp/...` if it needs the rest.

---

### `icmg-known-issue-recall.sh` (PostToolUseFailure on `Bash`)

When a Bash command errors, queries `icmg known-issue match` and injects any past resolution.

**Install:**

```json
{
  "hooks": {
    "PostToolUseFailure": [{
      "matcher": "Bash",
      "hooks": [{
        "type": "command",
        "command": "/path/to/examples/hooks/icmg-known-issue-recall.sh"
      }]
    }]
  }
}
```

**Behavior:** silent if no match; on match, agent receives `additionalContext` like "Past resolution found: #42 errors-resolved NullRef in Foo  Pattern: NullRef in Foo Fix: guard at L60".

---

## Manual Activation

Hooks are not auto-installed. Add the JSON snippet above to your project's `.claude/settings.local.json` (gitignored personal scope) or `.claude/settings.json` (team scope).

Reload via `/hooks` panel in Claude Code or restart the session.

---

## Composing Multiple Hooks

Multiple hooks per matcher run in array order. Example combined config:

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Read",
        "hooks": [
          {"type": "command", "command": "/path/to/icmg-shrink-read.sh"}
        ]
      },
      {
        "matcher": "Grep",
        "hooks": [
          {
            "type": "command",
            "command": "jq -n '{hookSpecificOutput:{hookEventName:\"PreToolUse\",permissionDecision:\"deny\",permissionDecisionReason:\"Use icmg run grep -rn \\\"pattern\\\" .\"}}'"
          }
        ]
      }
    ],
    "PostToolUse": [{
      "matcher": "Bash",
      "hooks": [{"type": "command", "command": "/path/to/icmg-cap-output.sh"}]
    }],
    "PostToolUseFailure": [{
      "matcher": "Bash",
      "hooks": [{"type": "command", "command": "/path/to/icmg-known-issue-recall.sh"}]
    }]
  }
}
```

---

## Testing a Hook

Pipe-test before installing:

```bash
echo '{"tool_input":{"file_path":"src/big-file.cs"}}' | bash icmg-shrink-read.sh
echo '{"tool_response":{"stdout":"'"$(yes hi | head -c 12000)"'"}}' | bash icmg-cap-output.sh
echo '{"tool_response":{"stderr":"NullReferenceException at Foo:64"}}' | bash icmg-known-issue-recall.sh
```

Each should output JSON or empty on no-match.

---

## Notes

- All hooks require `icmg` on `$PATH` and `jq` (any modern version).
- Hooks run with project root as CWD.
- `set -uo pipefail` — strict-ish; failures don't crash hook chain (PreToolUse hooks return empty = allow).
- No data leaves the local machine. Spill files live in OS temp dir.
