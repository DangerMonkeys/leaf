# Builds and runs the Leaf device emulator in a container, then opens the control panel.
#
# Nothing is installed on the host: the container has gcc and make, the repository is bind
# mounted, and the emulator serves its browser UI on a published port.
#
# Any trailing arguments are passed straight to leafsim, so the invocations in README.md work
# here too:
#
#   .\sim\run.ps1 -Scenario sim/recordings/example.igc --setting LAB_THERM_TRACK=1
[CmdletBinding()]
param(
  [int]$Port = 8080,
  [string]$Scenario = "",
  [double]$Speed = 1,
  [string]$Variant = "leaf_3_2_7",
  [switch]$NoBrowser,
  [switch]$Rebuild,
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$LeafsimArgs
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path

$command = "make -C sim VARIANT=$Variant -j`$(nproc)"
if ($Rebuild) { $command = "make -C sim clean >/dev/null && $command" }

$arguments = "--port $Port --speed $Speed"
if ($Scenario) { $arguments += " --scenario $Scenario --play" }
if ($LeafsimArgs) { $arguments += " " + ($LeafsimArgs -join " ") }
$command += " && ./sim/build/leafsim $arguments"

Write-Host "Building the Leaf emulator (first build takes a few minutes)..." -ForegroundColor Cyan
if (-not $NoBrowser) {
  Start-Job -ScriptBlock {
    param($url)
    # Give the build time to finish before the browser asks for the page.
    for ($i = 0; $i -lt 600; $i++) {
      try {
        Invoke-WebRequest -Uri $url -TimeoutSec 1 -UseBasicParsing | Out-Null
        Start-Process $url
        return
      } catch { Start-Sleep -Seconds 1 }
    }
  } -ArgumentList "http://localhost:$Port" | Out-Null
}

docker run --rm -it `
  -p "${Port}:${Port}" `
  -p "7431:7431/udp" `
  -v "${repoRoot}:/leaf" `
  -w /leaf `
  gcc:13 sh -c $command

exit $LASTEXITCODE
