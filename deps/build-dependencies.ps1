param([string]$VisualStudioPath = '')
$ErrorActionPreference = 'Stop'
$depRoot = $PSScriptRoot

function Get-VerifiedArchive([string]$Name, [string]$Url, [string]$Hash, [string]$Algorithm = 'SHA256') {
    $archive = Join-Path $depRoot $Name
    if (!(Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -Uri $Url -OutFile $archive
        # SourceForge may send a download page before the actual archive.
        if ($Name -like 'pthreads*' -and (Get-FileHash -LiteralPath $archive -Algorithm $Algorithm).Hash -ne $Hash) {
            $html = Get-Content -LiteralPath $archive -Raw
            $match = [regex]::Match($html, 'content="5; url=([^"]+)"')
            if ($match.Success) {
                $direct = [System.Net.WebUtility]::HtmlDecode($match.Groups[1].Value)
                if ([uri]$direct -and ([uri]$direct).Host -eq 'downloads.sourceforge.net' -and ([uri]$direct).Scheme -eq 'https') {
                    Invoke-WebRequest -Uri $direct -OutFile $archive
                }
            }
        }
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm $Algorithm).Hash -ne $Hash) {
        throw "Checksum mismatch: $Name. Remove the failed archive and retry."
    }
    return $archive
}

if (!$VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
}
if (!$VisualStudioPath) { throw 'Visual Studio 2022 C++ build tools are required.' }
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$cmake = Join-Path $VisualStudioPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (!(Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
# Import the x64 compiler environment into this PowerShell process only.
$devEnvironment = & cmd.exe /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the MSVC x64 environment.' }
foreach ($line in $devEnvironment) {
    $delimiter = $line.IndexOf('=')
    if ($delimiter -gt 0) {
        [Environment]::SetEnvironmentVariable($line.Substring(0, $delimiter), $line.Substring($delimiter + 1), 'Process')
    }
}

$toxArchive = Get-VerifiedArchive 'c-toxcore-v0.2.23.tar.gz' 'https://github.com/TokTok/c-toxcore/releases/download/v0.2.23/c-toxcore-v0.2.23.tar.gz' '15cdd006ed7793dfc657e340ef9f218f6637d2fe5b130704d39b961389bb6cd6'
$sodiumArchive = Get-VerifiedArchive 'libsodium-1.0.22-msvc.zip' 'https://download.libsodium.org/libsodium/releases/libsodium-1.0.22-msvc.zip' '3e03a726fac4bc09cb61d8f29d658ef7a5eca0811de59082130414f7ca2e4279'
$sodiumSourceArchive = Get-VerifiedArchive 'libsodium-1.0.22.tar.gz' 'https://download.libsodium.org/libsodium/releases/libsodium-1.0.22.tar.gz' 'adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349'
$pthreadArchive = Get-VerifiedArchive 'pthreads4w-code-v3.0.0.zip' 'https://downloads.sourceforge.net/project/pthreads4w/pthreads4w-code-v3.0.0.zip' '49e541b66c26ddaf812edb07b61d0553e2a5816ab002edc53a38a897db8ada6d0a096c98a9af73a8f40c94283df53094f76b429b09ac49862465d8697ed20013' 'SHA512'

$toxRoot = Join-Path $depRoot 'toxcore-full'
if (!(Test-Path -LiteralPath (Join-Path $toxRoot 'toxcore/tox.c'))) {
    New-Item -ItemType Directory -Path $toxRoot -Force | Out-Null
    # Apple-only symlink cannot be created without symlink rights on Windows.
    & tar.exe -xf $toxArchive -C $toxRoot --exclude './other/deploy/apple/LICENSE'
    if ($LASTEXITCODE -ne 0) { throw 'Tox source extraction failed.' }
}
if (!(Test-Path -LiteralPath (Join-Path $depRoot 'sodium-msvc/libsodium/include/sodium.h'))) {
    Expand-Archive -LiteralPath $sodiumArchive -DestinationPath (Join-Path $depRoot 'sodium-msvc') -Force
}
if (!(Test-Path -LiteralPath (Join-Path $depRoot 'libsodium-1.0.22/LICENSE'))) {
    & tar.exe -xf $sodiumSourceArchive -C $depRoot
    if ($LASTEXITCODE -ne 0) { throw 'libsodium source extraction failed.' }
}
$pthreadRoot = Join-Path $depRoot 'pthreads/pthreads4w-code-07053a521b0a9deb6db2a649cde1f828f2eb1f4f'
if (!(Test-Path -LiteralPath (Join-Path $pthreadRoot 'pthread.c'))) {
    Expand-Archive -LiteralPath $pthreadArchive -DestinationPath (Join-Path $depRoot 'pthreads') -Force
}
Push-Location $pthreadRoot
try {
    & nmake.exe /nologo VC-static
    if ($LASTEXITCODE -ne 0) { throw 'pthreads4w build failed.' }
} finally { Pop-Location }
$buildRoot = Join-Path $depRoot 'build-nmake'
$installRoot = Join-Path $depRoot 'install'
& $cmake -S $depRoot -B $buildRoot -G 'NMake Makefiles' '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_INSTALL_PREFIX=$installRoot"
if ($LASTEXITCODE -ne 0) { throw 'Tox configure failed.' }
& $cmake --build $buildRoot --config Release
if ($LASTEXITCODE -ne 0) { throw 'Tox build failed.' }
& $cmake --install $buildRoot --config Release
if ($LASTEXITCODE -ne 0) { throw 'Dependency install failed.' }
Write-Host "Static x64 dependencies are ready in $installRoot"
