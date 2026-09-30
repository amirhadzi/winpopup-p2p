param(
    [string]$Dependencies = (Join-Path $PSScriptRoot '..\deps\install'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 with Desktop development with C++ and CMake.' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'No Visual Studio C++ x64 toolchain found.' }
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeExe = $cmakeCommand.Source } else {
    $cmakeExe = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
# Import the compiler environment into this process only. All file operations remain in PowerShell.
$compilerEnvironment = & cmd.exe /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x64 compiler environment.' }
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
if (-not (Test-Path -LiteralPath (Join-Path $Dependencies 'lib\toxcore.lib'))) {
    throw 'Native dependencies are missing. Build the included deps package first; see BUILDING.md.'
}
& $cmakeExe -S $PSScriptRoot -B $BuildDirectory -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release "-DPOPUP_DEPS=$Dependencies"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmakeExe --build $BuildDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
if ($Test) {
    $ctestExe = Join-Path (Split-Path $cmakeExe) 'ctest.exe'
    & $ctestExe --test-dir $BuildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Application verification failed. See the CTest output above.' }
}
Write-Host "Built: $(Join-Path $BuildDirectory 'WinPopup.exe')"
