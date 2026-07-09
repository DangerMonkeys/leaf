param(
    [int]$Port = 4321,
    [switch]$Network,
    [switch]$NoForce
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$WebsiteRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepoRoot = (Resolve-Path (Join-Path $WebsiteRoot "..")).Path
$DocsTarget = (Resolve-Path (Join-Path $RepoRoot "docs")).Path
$DocsLink = Join-Path $WebsiteRoot "src\content\docs"

function Ensure-DocsLink {
    if ((Test-Path -LiteralPath $DocsLink -PathType Container)) {
        return
    }

    if (Test-Path -LiteralPath $DocsLink -PathType Leaf) {
        $content = (Get-Content -LiteralPath $DocsLink -Raw -ErrorAction SilentlyContinue).Trim()
        if ($content -ne "../../../docs") {
            throw "Refusing to replace unexpected file at $DocsLink"
        }
        Remove-Item -LiteralPath $DocsLink
    }

    New-Item -ItemType Junction -Path $DocsLink -Target $DocsTarget | Out-Null
}

function Get-NodePath {
    $localNode = Join-Path $WebsiteRoot ".local-node\node-v24.14.0-win-x64\node.exe"
    if (Test-Path -LiteralPath $localNode -PathType Leaf) {
        return $localNode
    }
    return "node"
}

Ensure-DocsLink

$node = Get-NodePath
$astro = Join-Path $WebsiteRoot "node_modules\astro\astro.js"
if (-not (Test-Path -LiteralPath $astro -PathType Leaf)) {
    throw "Astro CLI not found at $astro. Run dependency install for the website first."
}

$bindHost = "localhost"
if ($Network) {
    $bindHost = "0.0.0.0"
}

$astroArgs = @($astro, "dev", "--host", $bindHost, "--port", [string]$Port)
if (-not $NoForce) {
    $astroArgs += "--force"
}

Write-Host "Starting Leaf website preview..."
Write-Host "Local:   http://localhost:$Port/"
if ($Network) {
    $addresses = Get-NetIPConfiguration |
        Where-Object { $_.IPv4Address -and $_.NetAdapter.Status -eq "Up" } |
        ForEach-Object { $_.IPv4Address.IPAddress }

    foreach ($address in $addresses) {
        Write-Host "Network: http://$address`:$Port/"
    }
}

Push-Location $WebsiteRoot
try {
    & $node $astroArgs
}
finally {
    Pop-Location
}
