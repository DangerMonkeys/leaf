<#  Decode ESP32/ESP32‑S2/S3 (Xtensa) and ESP32‑C3/C6 (RISC‑V) backtraces.

Usage:
  powershell -ExecutionPolicy Bypass -File scripts/decode-backtrace.ps1 [-Backtrace "..."] [-ElfPath path]

If -Backtrace is omitted, the script will try clipboard text and then prompt.
It auto-selects the correct addr2line from %USERPROFILE%\.platformio\packages.
#>

param(
  [string]$Backtrace,
  [string]$ElfPath
)

function Write-Err($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red }

# --- 1) Acquire backtrace text ---
if (-not $Backtrace -or $Backtrace.Trim() -eq "") {
  try {
    $clip = Get-Clipboard -Raw -ErrorAction Stop
  } catch { $clip = "" }
  if ($clip -and ($clip -match "0x[0-9A-Fa-f]+")) {
    $Backtrace = $clip
    Write-Host "Using backtrace from clipboard." -ForegroundColor DarkCyan
  } else {
    Write-Host "*** Note that a backtrace on the clipboard will be processed automatically."
    $Backtrace = Read-Host "Paste your Backtrace line or just addresses (e.g. 0x4200F891:0x3FCC0450 ...)"
  }
}

# --- 2) Extract program counters (PCs) from the backtrace text ---
# Accepts:
#   "Backtrace: 0xAAA:0xBBB 0xCCC:0xDDD ..."  (pc:sp pairs)
#   "0xAAA 0xCCC ..."                         (pc-only)
#   commas/semicolons mixed in
function Get-PCsFromBacktrace([string]$text) {
  $norm = $text -replace '[,;]', ' '
  $matches = [regex]::Matches($norm, '0x[0-9A-Fa-f]+(?::0x[0-9A-Fa-f]+)?')
  $pcs = @()
  foreach ($m in $matches) {
    $token = $m.Value
    if ($token -match '^0x[0-9A-Fa-f]+:0x[0-9A-Fa-f]+$') {
      # "pc:sp" -> take the pc (left side)
      $pcs += $token.Split(':')[0]
    } else {
      $pcs += $token
    }
  }
  return $pcs
}

$pcs = Get-PCsFromBacktrace $Backtrace
if (-not $pcs -or $pcs.Count -eq 0) {
  Write-Err "No addresses found in input."
  exit 1
}

# --- 3) Locate the ELF for the current PlatformIO environment ---
function Resolve-ElfStrict {
  $pioEnv = $env:PIOENV
  if (-not $pioEnv -or -not $pioEnv.Trim()) {
    Write-Err "PIOENV is not set. Run from a PlatformIO terminal/task that sets $env:PIOENV, or export it manually."
    exit 1
  }
  $projectRoot = (Resolve-Path ".").Path
  $envDir = Join-Path $projectRoot ".pio/build/$pioEnv"
  $candidate = Join-Path $envDir "firmware.elf"
  if (-not (Test-Path $candidate)) {
    Write-Err "ELF not found for env '$pioEnv': $candidate`nBuild the project for this environment first, then retry."
    exit 1
  }
  return (Resolve-Path $candidate).Path
}

$ElfPath = Resolve-ElfStrict
Write-Host "Using PIOENV: $($env:PIOENV)" -ForegroundColor DarkCyan
Write-Host "Using ELF: $ElfPath" -ForegroundColor DarkCyan

# --- 4) Select the proper addr2line ---
function Find-Addr2line() {
  $pkgs = Join-Path $env:USERPROFILE ".platformio\packages"
  $candidates = @(
    "toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-addr2line.exe",
    "toolchain-xtensa-esp32\bin\xtensa-esp32-elf-addr2line.exe",
    "toolchain-riscv32-esp\bin\riscv32-unknown-elf-addr2line.exe",
    "toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-addr2line.exe"
  ) | ForEach-Object { Join-Path $pkgs $_ }

  foreach ($p in $candidates) {
    if (Test-Path $p) { return $p }
  }
  # last resort: hope addr2line is on PATH
  return "addr2line"
}

$Addr2linePath = Find-Addr2line
Write-Host "Using addr2line: $Addr2linePath" -ForegroundColor DarkCyan

# --- 5) Run addr2line on each PC ---
Write-Host "`nDecoding:" -ForegroundColor Cyan
$argList = @("-pfiaC", "-e", $ElfPath) + $pcs
$proc = Start-Process -FilePath $Addr2linePath -ArgumentList $argList -NoNewWindow -PassThru -RedirectStandardOutput "STDOUT.tmp" -RedirectStandardError "STDERR.tmp"
$proc.WaitForExit()

if ((Get-Item "STDERR.tmp").Length -gt 0) {
  Write-Host (Get-Content "STDERR.tmp" -Raw) -ForegroundColor Yellow
}
if ((Get-Item "STDOUT.tmp").Length -gt 0) {
  Get-Content "STDOUT.tmp"
}

Remove-Item -ErrorAction SilentlyContinue "STDOUT.tmp","STDERR.tmp"
