[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$BuildType = "Release",
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

$repoRootForWsl = $repoRoot -replace '\\', '/'
$wslRepoRoot = (& wsl.exe --exec wslpath -a $repoRootForWsl).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($wslRepoRoot)) {
    throw "Unable to resolve repository path in WSL. Is WSL installed?"
}

$cleanFlag = if ($Clean) { "1" } else { "0" }

$bash = @'
set -euo pipefail
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"

cd "$EQVITA_REPO_ROOT"

if [ ! -f "$VITASDK/share/vita.toolchain.cmake" ]; then
  echo "VitaSDK not found at $VITASDK. Install it from https://vitasdk.org/ first." >&2
  exit 1
fi

if [ "$EQVITA_CLEAN" = "1" ]; then
  rm -rf -- "$EQVITA_BUILD_DIR"
fi

cmake -S . -B "$EQVITA_BUILD_DIR" -DCMAKE_BUILD_TYPE="$EQVITA_BUILD_TYPE"
cmake --build "$EQVITA_BUILD_DIR"
'@

& wsl.exe --exec env "EQVITA_REPO_ROOT=$wslRepoRoot" "EQVITA_BUILD_DIR=$relativeBuildDir" "EQVITA_BUILD_TYPE=$BuildType" "EQVITA_CLEAN=$cleanFlag" bash -lc $bash
exit $LASTEXITCODE
