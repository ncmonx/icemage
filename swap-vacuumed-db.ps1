# swap-vacuumed-db.ps1 -- compact (VACUUM) the icmg brain DB safely.
# JALANKAN DARI POWERSHELL BIASA (bukan dari dalam GUI), SETELAH GUI + semua
# sesi icmg ditutup:
#   pwsh -File "D:\Data Kerja\Personal\AI\icemage\swap-vacuumed-db.ps1"
#
# Kunci aman: VACUUM dijalankan FRESH dari data.db SAAT INI (bukan snapshot lama),
# jadi TIDAK ada memori yang hilang seberapa pun lamanya kamu ngobrol sebelum ini.
# Verifikasi: tidak ada proses pemegang DB, integrity_check, jumlah baris masuk
# akal, backup penuh, swap, bersihkan WAL/SHM stale, rollback bila gagal.

$ErrorActionPreference = 'Stop'
$dir   = 'D:\Data Kerja\Personal\AI\icemage\.icmg'
$live  = Join-Path $dir 'data.db'
$fresh = Join-Path $dir 'data.freshvacuum.db'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$bak   = Join-Path $dir "data.preswap-$stamp.db"

Write-Host "=== icmg DB compact (fresh VACUUM, no memory loss) ===" -ForegroundColor Cyan

if (-not (Test-Path $live)) { throw "live DB tidak ada: $live" }

# 1) PASTIKAN tidak ada proses yang megang DB.
$busy = Get-Process icmg,icemage-code -ErrorAction SilentlyContinue
if ($busy) {
    Write-Host "STOP: masih ada proses yang megang DB:" -ForegroundColor Red
    $busy | Select-Object Id,ProcessName,Path | Format-Table -AutoSize
    throw "tutup GUI + semua sesi icmg dulu, lalu jalankan ulang."
}

$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $py) { $py = (Get-Command python3 -ErrorAction SilentlyContinue).Source }
if (-not $py) { throw "python tidak ditemukan di PATH (dibutuhkan untuk VACUUM aman)." }

# 2) Python: checkpoint WAL -> integrity -> VACUUM INTO fresh -> verifikasi fresh.
$pyScript = @'
import sqlite3, os, sys
live = sys.argv[1]; fresh = sys.argv[2]
if os.path.exists(fresh): os.remove(fresh)
c = sqlite3.connect(live, timeout=30)
# fold any leftover WAL into the main DB so the vacuum sees the latest commits
try: c.execute("PRAGMA wal_checkpoint(TRUNCATE)")
except Exception as e: print("checkpoint warn:", e)
ok = c.execute("PRAGMA integrity_check").fetchone()[0]
if ok != "ok":
    print("SOURCE_INTEGRITY_FAIL:", ok); sys.exit(2)
act = c.execute("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NULL").fetchone()[0]
c.execute("VACUUM INTO ?", (fresh.replace('\\','/'),))
c.close()
# verify the fresh copy
v = sqlite3.connect(fresh); cur = v.cursor()
vok = cur.execute("PRAGMA integrity_check").fetchone()[0]
vact = cur.execute("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NULL").fetchone()[0]
v.close()
if vok != "ok": print("FRESH_INTEGRITY_FAIL:", vok); sys.exit(3)
if vact != act: print(f"ROWCOUNT_MISMATCH live={act} fresh={vact}"); sys.exit(4)
if vact < 1000: print(f"SUSPICIOUS_LOW active={vact}"); sys.exit(5)
src_mb = os.path.getsize(live)/1e6; new_mb = os.path.getsize(fresh)/1e6
print(f"OK active={vact} integrity=ok src={src_mb:.1f}MB fresh={new_mb:.1f}MB")
'@
$tmp = Join-Path $env:TEMP "icmg_vacuum_$stamp.py"
$pyScript | Set-Content -Path $tmp -Encoding UTF8
Write-Host "VACUUM fresh dari data.db saat ini (checkpoint + integrity + verify)..."
$out = & $py $tmp $live $fresh 2>&1
Remove-Item $tmp -Force -ErrorAction SilentlyContinue
$out | Out-Host
if ($LASTEXITCODE -ne 0 -or ($out -join "`n") -notmatch '(^|\n)OK ') {
    throw "VACUUM/verifikasi gagal (exit $LASTEXITCODE) -- TIDAK menyentuh data.db. Aman."
}

# 3) backup penuh DB lama, lalu swap.
Write-Host "backup -> $(Split-Path $bak -Leaf)"
Copy-Item $live $bak -Force
try {
    $oldTmp = "$live.old-$stamp"
    Rename-Item $live $oldTmp -ErrorAction Stop
    Copy-Item $fresh $live -Force -ErrorAction Stop
    foreach ($s in @("$live-wal", "$live-shm")) {
        if (Test-Path $s) { Remove-Item $s -Force -ErrorAction SilentlyContinue }
    }
    Remove-Item $oldTmp -Force -ErrorAction SilentlyContinue
    Remove-Item $fresh  -Force -ErrorAction SilentlyContinue
    $newMB = [math]::Round((Get-Item $live).Length/1MB,1)
    Write-Host "SWAP OK -> data.db sekarang $newMB MB" -ForegroundColor Green
} catch {
    Write-Host "SWAP GAGAL: $($_.Exception.Message) -- rollback..." -ForegroundColor Red
    if ((Test-Path $oldTmp) -and -not (Test-Path $live)) { Rename-Item $oldTmp $live }
    elseif (-not (Test-Path $live)) { Copy-Item $bak $live -Force }
    throw
}

Write-Host ""
Write-Host "Selesai. Backup aman: $(Split-Path $bak -Leaf) (DB lama penuh)." -ForegroundColor Cyan
Write-Host "Buka lagi GUI -- DB sudah ramping, tanpa kehilangan memori."
