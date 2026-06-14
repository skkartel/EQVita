[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "build-wsl.ps1"
$cases = @(
    ".",
    "app",
    "plugin",
    "..\outside",
    "build-tools"
)

foreach ($case in $cases) {
    try {
        & $scriptPath -BuildDir $case -Clean
        throw "BuildDir '$case' was accepted unexpectedly."
    } catch {
        if ($_.Exception.Message -like "*accepted unexpectedly*") {
            throw
        }
        if ($_.Exception.Message -notlike "BuildDir must*") {
            throw "BuildDir '$case' failed after validation instead of being rejected by the guard: $($_.Exception.Message)"
        }
        Write-Host "Rejected unsafe BuildDir '$case'"
    }
}
