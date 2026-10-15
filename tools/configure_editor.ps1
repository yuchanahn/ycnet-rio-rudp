$ErrorActionPreference = 'Stop'
$task_root = Split-Path -Parent $PSScriptRoot
$task_vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $task_vswhere)) { throw 'Visual Studio Installer / vswhere not found' }
$task_vs = & $task_vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $task_vs) { throw 'Visual Studio C++ tools not found' }
$task_ninja = Join-Path $task_vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
if (-not (Test-Path -LiteralPath $task_ninja)) { throw 'Install the Visual Studio C++ CMake tools component (Ninja)' }
$task_tools_version = (Get-Content (Join-Path $task_vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$task_compiler = Join-Path $task_vs "VC/Tools/MSVC/$task_tools_version/bin/Hostx64/x64/cl.exe"
if (-not (Test-Path -LiteralPath $task_compiler)) { throw 'MSVC x64 compiler not found' }
$task_compare = if (Test-Path -LiteralPath (Join-Path $task_root '.dependencies/kcp/ikcp.c')) { 'ON' } else { 'OFF' }
# Configure only: the real VS build verifies MSVC. This database does not need
# a link test or a developer-shell environment; clangd discovers the SDK itself.
& cmake -S $task_root -B (Join-Path $task_root 'build_analysis') -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$task_ninja" -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_CXX_COMPILER=$task_compiler" -DCMAKE_CXX_COMPILER_WORKS=TRUE `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-Dycnet_build_comparisons=$task_compare"
if ($LASTEXITCODE -ne 0) { throw "Editor CMake configuration failed: $LASTEXITCODE" }
Write-Output 'Editor compilation database ready: build_analysis/compile_commands.json'
