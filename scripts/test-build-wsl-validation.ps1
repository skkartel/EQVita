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

$workflowPath = Join-Path $PSScriptRoot "..\.github\workflows\host-tests.yml"
$workflow = Get-Content -LiteralPath $workflowPath -Raw
$unsafeChecksumRedirect = 'sha256sum\s+build/app/EQVita\.vpk\s+build/plugin/eq_speaker\.skprx\s*>\s*build/EQVita\.sha256'
$safeChecksumWrite = 'sha256sum\s+build/app/EQVita\.vpk\s+build/plugin/eq_speaker\.skprx\s*\|\s*sudo\s+tee\s+build/EQVita\.sha256'

if ($workflow -match $unsafeChecksumRedirect) {
    throw "CI checksum step writes into Docker-owned build/ with shell redirection. Use sudo tee instead."
}

if ($workflow -notmatch $safeChecksumWrite) {
    throw "CI checksum step should write build/EQVita.sha256 via sudo tee."
}
