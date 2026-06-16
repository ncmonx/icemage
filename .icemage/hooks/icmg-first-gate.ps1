# icmg-first PreToolUse GATE (auto-provisioned). Denies a Bash command that is
# really a search/read better served by an icmg-aware native tool.
$ErrorActionPreference = 'SilentlyContinue'
$raw = [Console]::In.ReadToEnd()
function Allow { '{}'; exit 0 }
if ([string]::IsNullOrWhiteSpace($raw)) { Allow }
try { $ev = $raw | ConvertFrom-Json } catch { Allow }
$tool = [string]$ev.tool_name
if ($tool -ne 'Bash') { Allow }
$cmd = [string]$ev.tool_input.command
if ([string]::IsNullOrWhiteSpace($cmd)) { Allow }
# Strip quoted-string content so tokens inside commit messages / --args don't false-positive.
# e.g. git commit -m "find the bug"  ->  git commit -m ""  (no match)
$stripped = $cmd -replace '"[^"]*"', '""' -replace "'[^']*'", "''"
$redirect = $null
switch -Regex ($stripped) {
  '(^|[\s|;&])(grep|rg|ag)(\s|$)'        { $redirect = 'Grep (icmg-aware) instead of shelling out to grep/rg'; break }
  '(?i)(^|[\s|;&(])(Select-String|sls)(\s|$)'  { $redirect = 'Grep (icmg-aware) instead of Select-String/sls'; break }
  '(?i)(^|[\s|;&(])findstr(\s|$)'        { $redirect = 'Grep (icmg-aware) instead of findstr'; break }
  '(^|[\s|;&])find(\s|$)'                { $redirect = 'Glob instead of find'; break }
  '(?i)(^|[\s|;&(])(Get-ChildItem|gci)(\s|$)'  { $redirect = 'Glob instead of Get-ChildItem/gci'; break }
  '(^|[\s|;&])(cat|head|tail|less|more|type)(\s|$)' { $redirect = 'Read (line numbers, offset/limit) instead of cat/head/tail'; break }
  '(?i)(^|[\s|;&(])(Get-Content|gc)(\s|$)'      { $redirect = 'Read (line numbers, offset/limit) instead of Get-Content/gc'; break }
  '(^|[\s|;&])(ls|dir)(\s|$)'            { $redirect = 'Glob to list files instead of ls/dir'; break }
}
if ($null -eq $redirect) { Allow }
$reason = "icmg-first: use $redirect. (Bash is for real shell work, not search/read.) Do NOT retry another shell variant -- it is blocked again and wastes turns."
@{ hookSpecificOutput = @{ hookEventName = 'PreToolUse'; permissionDecision = 'deny'; permissionDecisionReason = $reason } } | ConvertTo-Json -Compress -Depth 6
exit 0
