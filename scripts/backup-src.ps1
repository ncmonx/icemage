$ts = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = ".icmg/backup/src-pre-v1.41.0-$ts.zip"
New-Item -ItemType Directory -Path .icmg/backup -Force | Out-Null
Compress-Archive -Path src, CMakeLists.txt, tests -DestinationPath $out -CompressionLevel Optimal
Get-Item $out | Select-Object Name, Length, FullName
