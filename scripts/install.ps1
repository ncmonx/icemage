# icmg one-line installer (Windows).
#   irm https://raw.githubusercontent.com/ncmonx/icemage/main/scripts/install.ps1 | iex
# Env overrides: ICMG_VERSION (e.g. 2.1.0), ICMG_BIN_DIR (default $HOME\bin).
$ErrorActionPreference = 'Stop'

$repo   = 'ncmonx/icemage'
$binDir = if ($env:ICMG_BIN_DIR) { $env:ICMG_BIN_DIR } else { Join-Path $HOME 'bin' }

$ver = $env:ICMG_VERSION
if (-not $ver) {
    $rel = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest"
    $ver = ($rel.tag_name -replace '^v', '')
}
if (-not $ver) { throw "icmg: could not resolve latest version" }

$asset = "icmg-$ver-win-x64.zip"
$url    = "https://github.com/$repo/releases/download/v$ver/$asset"
$tmp    = Join-Path $env:TEMP "icmg-install-$ver"
New-Item -ItemType Directory -Force $tmp | Out-Null

Write-Host "icmg: downloading $asset ..."
Invoke-WebRequest $url -OutFile (Join-Path $tmp $asset)

# Best-effort checksum verify.
try {
    Invoke-WebRequest "$url.sha256" -OutFile (Join-Path $tmp "$asset.sha256")
    $expected = ((Get-Content (Join-Path $tmp "$asset.sha256")) -split '\s+')[0].Trim().ToLower()
    $actual   = (Get-FileHash (Join-Path $tmp $asset) -Algorithm SHA256).Hash.ToLower()
    if ($expected -and ($actual -ne $expected)) { throw "SHA256 mismatch (expected $expected, got $actual)" }
} catch {
    Write-Warning "icmg: checksum verify skipped ($_)"
}

New-Item -ItemType Directory -Force $binDir | Out-Null
Expand-Archive -Path (Join-Path $tmp $asset) -DestinationPath $binDir -Force
Remove-Item $tmp -Recurse -Force

Write-Host "icmg: installed $ver to $binDir"
if (($env:PATH -split ';') -notcontains $binDir) {
    Write-Host "icmg: add to PATH ->  `$env:PATH = `"$binDir;`$env:PATH`""
    Write-Host "icmg: persist     ->  setx PATH `"$binDir;`$env:PATH`""
}
& (Join-Path $binDir 'icmg.exe') --version
