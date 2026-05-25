param([string]$ver = "1.40.0")
$ErrorActionPreference = 'Stop'
$pkg = "C:\temp\icmg-pkg-$ver"
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory -Path $pkg | Out-Null
Copy-Item "D:\Data Kerja\Personal\AI\icm-graph\build\icmg.exe" $pkg
foreach ($d in 'libtree-sitter-0.26.dll','libwinpthread-1.dll','libzstd.dll','wasmtime.dll') {
    Copy-Item "C:\msys64\mingw64\bin\$d" $pkg
}
foreach ($d in 'onnxruntime.dll','onnxruntime_providers_shared.dll') {
    Copy-Item "D:\Data Kerja\Personal\AI\icm-graph\third_party\onnxruntime\lib\$d" $pkg
}
$zip = "D:\Data Kerja\Personal\AI\icm-graph\icmg-$ver-win-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$pkg\*" -DestinationPath $zip
$h = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$sha = "$zip.sha256"
[IO.File]::WriteAllText($sha, "$h  icmg-$ver-win-x64.zip", [System.Text.Encoding]::ASCII)
Get-ChildItem $zip, $sha | Select-Object Name, Length | Format-Table
