[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot

function Fail {
    param([string]$Message)
    throw "Release hygiene failed: $Message"
}

function Read-RepoText {
    param([string]$RelativePath)
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Fail "Missing file: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Get-RegexValue {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Name
    )
    $match = [regex]::Match($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if (-not $match.Success) {
        Fail "Could not find $Name"
    }
    return $match.Groups[1].Value
}

function Assert-RepoFile {
    param([string]$RelativePath)
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Fail "Missing file: $RelativePath"
    }
}

function Get-CMakeTokens {
    param([string]$Text)
    $matches = [regex]::Matches($Text, '"(?:[^"\\]|\\.)*"|[^\s()]+')
    $tokens = New-Object System.Collections.Generic.List[string]
    foreach ($match in $matches) {
        $token = $match.Value
        if ($token.StartsWith('"') -and $token.EndsWith('"')) {
            $token = $token.Substring(1, $token.Length - 2)
        }
        [void]$tokens.Add($token)
    }
    return $tokens
}

function Get-VpkFilePairs {
    param([string]$CMakeText)
    $match = [regex]::Match(
        $CMakeText,
        '(?s)vita_create_vpk\s*\((.*?)\)\s*(?:if|$)',
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    )
    if (-not $match.Success) {
        Fail "Could not find vita_create_vpk block"
    }

    $tokens = Get-CMakeTokens $match.Groups[1].Value
    $fileIndex = -1
    for ($i = 0; $i -lt $tokens.Count; ++$i) {
        if ($tokens[$i] -eq 'FILE') {
            $fileIndex = $i
            break
        }
    }
    if ($fileIndex -lt 0) {
        Fail "vita_create_vpk block has no FILE list"
    }

    $fileTokens = @()
    for ($i = $fileIndex + 1; $i -lt $tokens.Count; ++$i) {
        [array]$fileTokens += $tokens[$i]
    }
    if (($fileTokens.Count % 2) -ne 0) {
        Fail "vita_create_vpk FILE list should use source/destination pairs"
    }

    $pairs = @()
    for ($i = 0; $i -lt $fileTokens.Count; $i += 2) {
        $pairs += [pscustomobject]@{
            Source = $fileTokens[$i]
            Destination = $fileTokens[$i + 1]
        }
    }
    return $pairs
}

function Get-PngInfo {
    param([string]$RelativePath)
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Fail "Missing PNG: $RelativePath"
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 33) {
        Fail "$RelativePath is too small to be a valid PNG"
    }

    [byte[]]$signature = 137, 80, 78, 71, 13, 10, 26, 10
    for ($i = 0; $i -lt $signature.Length; ++$i) {
        if ($bytes[$i] -ne $signature[$i]) {
            Fail "$RelativePath is not a PNG"
        }
    }

    $width = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $height = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    return [pscustomobject]@{
        Width = $width
        Height = $height
        BitDepth = [int]$bytes[24]
        ColorType = [int]$bytes[25]
    }
}

function Assert-IndexedPng {
    param(
        [string]$RelativePath,
        [int]$ExpectedWidth,
        [int]$ExpectedHeight
    )

    $info = Get-PngInfo $RelativePath
    if ($info.Width -ne $ExpectedWidth -or $info.Height -ne $ExpectedHeight) {
        Fail "$RelativePath should be ${ExpectedWidth}x${ExpectedHeight}, got $($info.Width)x$($info.Height)"
    }
    if ($info.BitDepth -ne 8 -or $info.ColorType -ne 3) {
        Fail "$RelativePath should be an 8-bit indexed PNG, got bit depth $($info.BitDepth), color type $($info.ColorType)"
    }
}

function Assert-MarkdownLinks {
    $markdownFiles = @(
        Get-Item -LiteralPath (Join-Path $RepoRoot 'README.md')
    ) + @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'docs') -Filter '*.md' -File -Recurse)

    foreach ($file in $markdownFiles) {
        $text = Get-Content -LiteralPath $file.FullName -Raw
        $matches = [regex]::Matches($text, '!?\[[^\]]+\]\(([^)]+)\)')
        foreach ($match in $matches) {
            $target = $match.Groups[1].Value.Trim()
            if ($target.Length -eq 0) {
                continue
            }
            if ($target -match '^(https?:|mailto:)' -or $target.StartsWith('#')) {
                continue
            }
            if ($target.StartsWith('<') -and $target.EndsWith('>')) {
                $target = $target.Substring(1, $target.Length - 2)
            }
            $target = ($target -split '#', 2)[0]
            if ($target.Length -eq 0) {
                continue
            }

            try {
                $target = [System.Uri]::UnescapeDataString($target)
            } catch {
                Fail "Bad Markdown link target '$target' in $($file.Name)"
            }

            $baseDir = Split-Path -Parent $file.FullName
            $resolved = [System.IO.Path]::GetFullPath((Join-Path $baseDir $target))
            if (-not (Test-Path -LiteralPath $resolved)) {
                $relativeMarkdown = [System.IO.Path]::GetRelativePath($RepoRoot, $file.FullName)
                Fail "Broken Markdown link in ${relativeMarkdown}: $($match.Groups[1].Value)"
            }
        }
    }
}

$shared = Read-RepoText 'common/eq_shared.h'
$exports = Read-RepoText 'plugin/exports.yml'
$appCMake = Read-RepoText 'app/CMakeLists.txt'
$readme = Read-RepoText 'README.md'

$major = [int](Get-RegexValue $shared '^\s*#define\s+EQ_VERSION_MAJOR\s+(\d+)' 'EQ_VERSION_MAJOR')
$minor = [int](Get-RegexValue $shared '^\s*#define\s+EQ_VERSION_MINOR\s+(\d+)' 'EQ_VERSION_MINOR')
$patch = [int](Get-RegexValue $shared '^\s*#define\s+EQ_VERSION_PATCH\s+(\d+)' 'EQ_VERSION_PATCH')
$fullVersion = "$major.$minor.$patch"
$expectedVitaVersion = '{0:D2}.{1:D2}' -f $major, $minor

$exportsMajor = [int](Get-RegexValue $exports '^\s*major:\s*(\d+)' 'plugin exports major')
$exportsMinor = [int](Get-RegexValue $exports '^\s*minor:\s*(\d+)' 'plugin exports minor')
$vitaVersion = Get-RegexValue $appCMake 'set\s*\(\s*VITA_VERSION\s+"([^"]+)"\s*\)' 'app VITA_VERSION'
$readmeAbi = Get-RegexValue $readme 'Current app/plugin ABI:\s*`?([0-9]+\.[0-9]+\.[0-9]+)`?' 'README ABI text'

if ($exportsMajor -ne $major -or $exportsMinor -ne $minor) {
    Fail "plugin/exports.yml version is $exportsMajor.$exportsMinor, expected $major.$minor"
}
if ($vitaVersion -ne $expectedVitaVersion) {
    Fail "app/CMakeLists.txt VITA_VERSION is $vitaVersion, expected $expectedVitaVersion"
}
if ($readmeAbi -ne $fullVersion) {
    Fail "README ABI is $readmeAbi, expected $fullVersion"
}

$audioExtensions = @('.ogg', '.mp3', '.wav', '.flac')
$pairs = Get-VpkFilePairs $appCMake
foreach ($pair in $pairs) {
    Assert-RepoFile ("app/{0}" -f $pair.Source)
    foreach ($packagedPath in @($pair.Source, $pair.Destination)) {
        $extension = [System.IO.Path]::GetExtension($packagedPath).ToLowerInvariant()
        if ($audioExtensions -contains $extension) {
            Fail "Packaged audio asset is not allowed in VPK: $packagedPath"
        }
    }
}

Assert-IndexedPng 'app/sce_sys/icon0.png' 128 128
Assert-IndexedPng 'app/sce_sys/livearea/contents/bg.png' 840 500
Assert-IndexedPng 'app/sce_sys/livearea/contents/startup.png' 280 158
Assert-MarkdownLinks

Write-Host "Release hygiene checks passed for EQVita $fullVersion"
