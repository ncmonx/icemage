# Contributing to Icemage

## Workflow

All changes to `main` must go through a pull request — no direct pushes.

```
git checkout -b fix/your-description
# make changes
git commit -m "fix: outcome-first description"
git push origin fix/your-description
# open PR → merge
```

## Branch naming

| Type | Pattern |
|------|---------|
| Bug fix | `fix/short-description` |
| Feature | `feat/short-description` |
| Docs | `docs/short-description` |
| CI | `ci/short-description` |

## Commit style

Outcome-first, present tense:

```
fix: update --apply now extracts zip correctly on Git Bash
feat: daemon IPC cuts hook latency from 360ms to 5ms
docs: add OpenSSF Scorecard badge
```

Not: `Fixed the extraction bug` / `Added feature`

## Building

```bash
# Windows / MSYS2
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Testing

Run the full test suite before submitting:

```bash
ctest --test-dir build --output-on-failure
```

## Reporting bugs

Use the [bug report template](.github/ISSUE_TEMPLATE/bug_report.yml).  
Security vulnerabilities: email ncmonx@hotmail.com (do not open a public issue).
