param([string]$ver = "1.43.0")
$ErrorActionPreference = 'Stop'
$pkg = "C:\temp\icmg-pkg-$ver"
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory -Path $pkg | Out-Null

$buildDir = "D:\Data Kerja\Personal\AI\icm-graph\build-msvc-full"
Copy-Item "$buildDir\icmg.exe" $pkg
# llama+ggml DLLs in bin/ subdir
foreach ($d in @('llama.dll','ggml.dll','ggml-base.dll','ggml-cpu.dll','ggml-vulkan.dll')) {
    if (Test-Path "$buildDir\bin\$d") { Copy-Item "$buildDir\bin\$d" $pkg }
}
# top-level DLLs (onnxruntime + tree-sitter copied during build)
foreach ($d in @('onnxruntime.dll','onnxruntime_providers_shared.dll')) {
    if (Test-Path "$buildDir\$d") { Copy-Item "$buildDir\$d" $pkg }
}
# Vulkan loader from system
if (Test-Path 'C:\Windows\System32\vulkan-1.dll') { Copy-Item 'C:\Windows\System32\vulkan-1.dll' $pkg }

$zip = "D:\Data Kerja\Personal\AI\icm-graph\icmg-$ver-win-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$pkg\*" -DestinationPath $zip
$h = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$sha = "$zip.sha256"
[IO.File]::WriteAllText($sha, "$h  icmg-$ver-win-x64.zip", [System.Text.Encoding]::ASCII)
Get-ChildItem $zip, $sha | Select-Object Name, Length | Format-Table
