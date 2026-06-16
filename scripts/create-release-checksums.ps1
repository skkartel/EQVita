[CmdletBinding()]
param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = $BuildDir.Trim()
if ([string]::IsNullOrWhiteSpace($BuildDir) -or
    [System.IO.Path]::IsPathRooted($BuildDir) -or
    $BuildDir -match '(^|[\\/])\.\.([\\/]|$)') {
    throw "BuildDir must be a relative path inside the repository."
}

$buildPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDir))
$repoRootWithSeparator = $RepoRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $buildPath.StartsWith($repoRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildDir must stay inside the repository."
}

$artifacts = @(
    Join-Path $buildPath "app/EQVita.vpk"
    Join-Path $buildPath "plugin/eq_speaker.skprx"
)

foreach ($artifact in $artifacts) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Missing artifact: $artifact"
    }
}

$checksumPath = Join-Path $buildPath "EQVita.sha256"
$buildPathWithSeparator = $buildPath.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
$lines = foreach ($artifact in $artifacts) {
    $artifactPath = [System.IO.Path]::GetFullPath($artifact)
    if (-not $artifactPath.StartsWith($buildPathWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Artifact must stay inside the build directory: $artifactPath"
    }

    $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = $artifactPath.Substring($buildPathWithSeparator.Length).Replace('\', '/')
    "$hash  $relative"
}

Set-Content -LiteralPath $checksumPath -Value $lines -Encoding ascii
Write-Host "Wrote $checksumPath"
