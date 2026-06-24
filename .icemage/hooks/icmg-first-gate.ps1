# icmg-first PreToolUse GATE v4 (auto-provisioned). For a Bash command that is
# really a search/list/read, TRANSLATE it to the matching icmg call and RETURN
# THE RESULT (decision:block + additionalContext) instead of just denying.
# Real shell work passes through untouched.
$ErrorActionPreference = 'SilentlyContinue'
$raw = [Console]::In.ReadToEnd()
function Allow { '{}'; exit 0 }
if ([string]::IsNullOrWhiteSpace($raw)) { Allow }
try { $ev = $raw | ConvertFrom-Json } catch { Allow }
if ([string]$ev.tool_name -ne 'Bash') { Allow }
$cmd = [string]$ev.tool_input.command
if ([string]::IsNullOrWhiteSpace($cmd)) { Allow }
# --- BUILD-DISCIPLINE GATE (CLAUDE.md rule #4) --------------------------------
# A raw `cmake --build ...` / `msbuild ...` is NOT the ship-grade build: it uses
# a different toolchain + output dir than build.ps1 (MSVC 2026, vcvars, vcpkg,
# C:\icmg-build). Only ENforced where build.ps1 actually exists in cwd, so the
# rule is self-scoping (other projects fall through). Runs BEFORE the icmg-allow
# below, but the build pattern is head-anchored (cmake/msbuild is the command
# being run) so `icmg run cmake --build` and `--target icmg` are handled right.
if (Test-Path 'build.ps1') {
  # icmg-headed command? not a raw build -- skip (handled by icmg-allow anyway).
  if ($cmd -notmatch '(?i)(^|[|;&]\s*)icmg(\.exe)?\s') {
    $bscan = [regex]::Replace($cmd, '"[^"]*"', '""')
    $bscan = [regex]::Replace($bscan, "'[^']*'", "''")
    if ($bscan -match '(?i)(^|[\s|;&])cmake\s+.*--build' -or $bscan -match '(?i)(^|[\s|;&])msbuild(\s|$)') {
      $r = "the ship-grade build (CLAUDE.md rule #4): run ``pwsh -File build.ps1`` (Target: icmg|test|both, add -RunTests for ctest). Raw cmake/msbuild uses a different toolchain + output dir and is NOT a valid ship build. Do NOT retry another raw build variant."
      @{ hookSpecificOutput = @{ hookEventName = 'PreToolUse'; permissionDecision = 'deny'; permissionDecisionReason = $r } } | ConvertTo-Json -Compress -Depth 6
      exit 0
    }
  }
}
# Already an icmg command? Let it through -- a trailing | head/Select-Object that
# just pages icmg's own output must NOT be mistaken for a raw read/search.
if ($cmd -match '(?i)(^|[\s|;&(])icmg(\.exe)?(\s|$)') { Allow }

# Commands that reference an absolute path (X:\... or X:/...) or a PowerShell
# $variable target a location the icmg graph (scoped to the ACTIVE project)
# cannot resolve -- translating them would either error or return the wrong
# project's files. Let those run as real shell so the agent sees true output.
if ($cmd -match '(?i)\b[A-Za-z]:[\\/]' -or $cmd -match '\$\w') { Allow }

$icmg = if ($env:ICMG_EXE) { $env:ICMG_EXE } else { 'icmg' }
$rg = if ($env:RG_EXE) { $env:RG_EXE } else { 'rg' }
# Gap #1 (2026-06-16): the gate matched trigger words anywhere in the command
# line, so a word that appears only INSIDE a quoted argument (e.g.
# `git commit -m "fix grep bug"`) was wrongly blocked as a search. Blank the
# CONTENTS of quoted strings -> $scan keeps the command STRUCTURE but drops
# quoted text, so trigger detection sees real command-head tokens only.
# Pattern/file extraction below still reads the ORIGINAL $cmd.
$scan = [regex]::Replace($cmd, '"[^"]*"', '""')
$scan = [regex]::Replace($scan, "'[^']*'", "''")
$globExt = $null
if ($cmd -match '\*\.([A-Za-z0-9]+)') { $globExt = '**/*.' + $Matches[1] }
function RunIcmg([string[]]$a) { try { (& $icmg @a 2>&1 | Out-String).Trim() } catch { '' } }
function RunRg([string[]]$a) { try { (& $rg @a 2>&1 | Out-String).Trim() } catch { '' } }
function Emit([string]$label, [string]$out) {
  if ($null -eq $out) { $out = '' }
  # If icmg produced nothing (project not indexed, path outside the active
  # project, or it errored), DO NOT block -- fall through so the real shell
  # command runs and the agent isn't left blind with an empty result.
  if ([string]::IsNullOrWhiteSpace($out)) { Allow }
  if ($out.Length -gt 20000) { $out = $out.Substring(0,20000) + "`n[output truncated to 20k chars]" }
  $o = [ordered]@{
    hookSpecificOutput = [ordered]@{ hookEventName = 'PreToolUse'; additionalContext = "[$label]`n$out" }
    decision = 'block'
    reason = 'icmg ran the equivalent of your shell command; the result is in additionalContext above -- use it directly. Do NOT re-run via shell (blocked again, wastes turns).'
  }
  $o | ConvertTo-Json -Compress -Depth 6
  exit 0
}
function Deny([string]$redirect) {
  $reason = "icmg-first: use $redirect. (Bash is for real shell work, not search/read.) Do NOT retry another shell variant -- it is blocked again and wastes turns."
  @{ hookSpecificOutput = @{ hookEventName = 'PreToolUse'; permissionDecision = 'deny'; permissionDecisionReason = $reason } } | ConvertTo-Json -Compress -Depth 6
  exit 0
}
function EmitGrep([string]$pat) {
  # icmg grep shells rg unquoted -> regex metachars (|, (), &) break it.
  # Route those (or any icmg shell-error) straight to rg (argv-safe).
  if ($pat -match '[|()&<>`;]') {
    $a = @('-e',$pat); if ($globExt) { $a += @('-g',$globExt) }; $a += @('--','.')
    Emit 'rg' (RunRg $a)
  }
  $ia = @('grep','-e',$pat); if ($globExt) { $ia += @('--glob',$globExt) }; $ia += @('--','.')
  $out = RunIcmg $ia
  if ($out -match "is not recognized|CommandNotFound|The term '") {
    $a = @('-e',$pat); if ($globExt) { $a += @('-g',$globExt) }; $a += @('--','.')
    Emit 'rg' (RunRg $a)
  }
  Emit 'icmg grep' $out
}

# --- SEARCH family -> icmg grep (return matches) ---
if ($scan -match '(?i)\b(Select-String|sls)\b') {
  $pat = $null
  if     ($cmd -match "(?i)-Pattern\s+'([^']+)'") { $pat = $Matches[1] }
  elseif ($cmd -match '(?i)-Pattern\s+"([^"]+)"') { $pat = $Matches[1] }
  elseif ($cmd -match '(?i)-Pattern\s+([^\s|]+)')  { $pat = $Matches[1] }
  if ($pat) { EmitGrep $pat }
  Deny 'Grep (icmg-aware) instead of Select-String/sls'
}
if ($scan -match '(?i)(^|[\s|;&])(grep|rg|ag)\s') {
  $pat = $null; $rest = ''
  if ($cmd -match '(?i)(?:^|[\s|;&])(?:grep|rg|ag)\s+(.+)$') { $rest = $Matches[1] }
  if     ($rest -match "^\s*'([^']+)'") { $pat = $Matches[1] }
  elseif ($rest -match '^\s*"([^"]+)"') { $pat = $Matches[1] }
  else { foreach ($t in ($rest -split '\s+' | Where-Object { $_ -ne '' })) { if ($t -notmatch '^-') { $pat = $t.Trim('"',"'"); break } } }
  if ($pat) { EmitGrep $pat }
  Deny 'Grep (icmg-aware) instead of shelling out to grep/rg'
}
if ($scan -match '(?i)(^|[\s|;&])findstr\s') {
  $pat = $null; $rest = ''
  if ($cmd -match '(?i)(?:^|[\s|;&])findstr\s+(.+)$') { $rest = $Matches[1] }
  if ($rest -match '"([^"]+)"') { $pat = $Matches[1] }
  else { foreach ($t in ($rest -split '\s+' | Where-Object { $_ -ne '' })) { if ($t -notmatch '^/') { $pat = $t.Trim('"'); break } } }
  if ($pat) { EmitGrep $pat }
  Deny 'Grep (icmg-aware) instead of findstr'
}

# --- LIST family -> icmg files (return file list) ---
if ($scan -match '(?i)(^|[\s|;&(])(Get-ChildItem|gci|ls|dir|find)(\s|$)') {
  if ($globExt) { Emit 'icmg files' (RunIcmg @('files','.','--glob',$globExt)) }
  elseif ($cmd -match '(?i)-Filter\s+["'']?([^"''\s]+)') { Emit 'icmg files' (RunIcmg @('files','.','--glob', ('**/' + $Matches[1]))) }
  Emit 'icmg files' (RunIcmg @('files','.'))
}

# --- READ family -> icmg context (return file bundle) ---
if ($scan -match '(?i)(^|[\s|;&(])(cat|head|tail|less|more|type|Get-Content|gc)(\s|$)') {
  $file = $null
  if ($cmd -match '(?i)(?:cat|head|tail|less|more|type|Get-Content|gc)\s+(?:-\S+\s+)*["'']?([^\s"''|]+)') { $file = $Matches[1] }
  if ($file) { Emit 'icmg context' (RunIcmg @('context',$file)) }
  Deny 'Read (line numbers, offset/limit) instead of cat/head/tail/Get-Content'
}

Allow
