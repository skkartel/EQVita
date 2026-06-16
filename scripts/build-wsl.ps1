[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$BuildType = "Release",
    [string]$Distro = "Ubuntu",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$BuildDir = $BuildDir.Trim()
if ([string]::IsNullOrWhiteSpace($BuildDir) -or
    [System.IO.Path]::IsPathRooted($BuildDir) -or
    $BuildDir -match '(^|[\\/])\.\.([\\/]|$)') {
    throw "BuildDir must be a relative path inside the repository."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$candidateBuildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$repoRootWithSeparator = $repoRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidateBuildPath.StartsWith($repoRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildDir must stay inside the repository."
}

$relativeBuildDir = $candidateBuildPath.Substring($repoRootWithSeparator.Length) -replace '\\', '/'
$allowedBuildDirPattern = '^build($|[-_.](host|vita|debug|release|ci|test|tests|clean|[0-9]+)([-_.][A-Za-z0-9]+)*)$'
if ($relativeBuildDir -notmatch $allowedBuildDirPattern) {
    throw "BuildDir must be a generated build directory such as build or build-vita."
}

$Distro = $Distro.Trim()
if ([string]::IsNullOrWhiteSpace($Distro)) {
    throw "Distro must name the WSL distro to use."
}

& wsl.exe -d $Distro -e true
if ($LASTEXITCODE -ne 0) {
    throw "WSL distro '$Distro' is not available. Install it or pass -Distro with the correct distro name."
}

$repoRootForWsl = $repoRoot -replace '\\', '/'
$wslRepoRoot = (& wsl.exe -d $Distro -e wslpath -a $repoRootForWsl).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($wslRepoRoot)) {
    throw "Unable to resolve repository path in WSL. Is WSL installed?"
}

$cleanFlag = if ($Clean) { "1" } else { "0" }

$wslScript = (& wsl.exe -d $Distro -e wslpath -a (($PSScriptRoot + "\build-wsl.sh") -replace '\\', '/')).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($wslScript)) {
    throw "Unable to resolve scripts/build-wsl.sh in WSL."
}

& wsl.exe -d $Distro -e env "EQVITA_REPO_ROOT=$wslRepoRoot" "EQVITA_ARTIFACT_DIR=$relativeBuildDir" "EQVITA_BUILD_TYPE=$BuildType" "EQVITA_CLEAN=$cleanFlag" bash "$wslScript"
$exitCode = $LASTEXITCODE

exit $exitCode
