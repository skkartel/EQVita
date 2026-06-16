[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "build-wsl.ps1"
$wslScriptPath = Join-Path $PSScriptRoot "build-wsl.sh"
$scriptSource = Get-Content -LiteralPath $scriptPath -Raw

if (-not (Test-Path -LiteralPath $wslScriptPath)) {
    throw "scripts/build-wsl.sh should provide a WSL-native build entry point."
}

$wslScriptSource = Get-Content -LiteralPath $wslScriptPath -Raw

if ($wslScriptSource -notmatch 'native_build_dir=') {
    throw "build-wsl.sh should build in a WSL-native cache directory."
}

if ($wslScriptSource -match 'cmake\s+-S\s+\.\s+-B\s+"\$EQVITA_ARTIFACT_DIR"') {
    throw "build-wsl.sh should not build directly in the Windows-mounted artifact directory."
}

if ($wslScriptSource -notmatch 'cp\s+-f\s+"\$native_build_dir/app/EQVita\.vpk"\s+"\$EQVITA_ARTIFACT_DIR/app/EQVita\.vpk"') {
    throw "build-wsl.sh should copy the VPK artifact back to the requested artifact directory."
}

if ($wslScriptSource -notmatch 'cp\s+-f\s+"\$native_build_dir/plugin/eq_speaker\.skprx"\s+"\$EQVITA_ARTIFACT_DIR/plugin/eq_speaker\.skprx"') {
    throw "build-wsl.sh should copy the plugin artifact back to the requested artifact directory."
}

if ($scriptSource -notmatch '\[string\]\$Distro\s*=\s*"Ubuntu"') {
    throw "build-wsl.ps1 should default to the Ubuntu WSL distro explicitly."
}

if ($scriptSource -notmatch 'wsl\.exe\s+-d\s+\$Distro\s+-e') {
    throw "build-wsl.ps1 should invoke WSL with an explicit distro."
}

if ($scriptSource -match 'wsl\.exe\s+--exec') {
    throw "build-wsl.ps1 should not rely on the default WSL distro via --exec."
}

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
