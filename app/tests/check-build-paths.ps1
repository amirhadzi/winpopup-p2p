param(
    [string]$Binary,
    [string]$SourceRoot,
    [string]$BuildRoot,
    [string]$DependenciesRoot,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'

function Find-PrivatePath([byte[]]$Bytes, [string[]]$Roots) {
    # Scan narrow strings plus wide strings at both possible byte alignments.
    $views = @([Text.Encoding]::UTF8.GetString($Bytes),
        [Text.Encoding]::Unicode.GetString($Bytes))
    if ($Bytes.Length -gt 1) { $views += [Text.Encoding]::Unicode.GetString($Bytes, 1, $Bytes.Length - 1) }
    foreach ($view in $views) {
        $normalized = $view.Replace('\', '/')
        if ($normalized -match '(?i)[a-z]:/(users|documents and settings)/|/(home|Users)/[^/\s\x00]+/') {
            return $true
        }
        foreach ($root in $Roots) {
            if ($root -and $normalized.IndexOf($root.Replace('\', '/').TrimEnd('/'), [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                return $true
            }
        }
    }
    return $false
}

if ($SelfTest) {
    $cases = @(
        @{Text='C:\Users\PrivateBuilder\repo\tox.c'; Bad=$true},
        @{Text='c:/users/privatebuilder/repo/tox.c'; Bad=$true},
        @{Text='D:\Documents and Settings\Builder\file.c'; Bad=$true},
        @{Text='/home/builder/project/file.c'; Bad=$true},
        @{Text='/Users/builder/project/file.c'; Bad=$true},
        @{Text='E:\private-ci\job\src\file.c'; Bad=$true},
        @{Text='deps\toxcore-full\toxcore\Messenger.c'; Bad=$false},
        @{Text='app/src/main.cpp WinPopup.pdb'; Bad=$false}
    )
    $count = 0
    foreach ($case in $cases) {
        foreach ($encoding in @([Text.Encoding]::UTF8, [Text.Encoding]::Unicode)) {
            foreach ($offset in @(0,1)) {
                [byte[]]$bytes = @([byte]0) * $offset + $encoding.GetBytes($case.Text)
                if ((Find-PrivatePath $bytes @('E:\private-ci\job')) -ne $case.Bad) { throw 'Build-path scanner self-test failed.' }
                $count++
            }
        }
    }
    Write-Output "$count build-path scanner checks passed."
    exit 0
}
if (!$Binary) { throw 'Specify the binary to verify.' }
$roots = @($SourceRoot, $BuildRoot, $DependenciesRoot, $env:USERPROFILE)
if (Find-PrivatePath ([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Binary))) $roots) {
    throw 'Private build path detected in the executable. Rebuild all dependencies with path remapping before distribution.'
}
Write-Output 'Build-path privacy check passed: no user-profile paths or supplied build roots in the executable.'
