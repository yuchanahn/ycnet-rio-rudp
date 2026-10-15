param([switch]$check_only, [string]$output_name = 'protocol_comparison_policy')
$ErrorActionPreference = 'Stop'
$task_root = Split-Path -Parent $PSScriptRoot
$task_dependency = Join-Path $task_root '.dependencies/kcp'
$task_commit = 'b1a7a2101dcbb96017681a500d6b82bbe5a88766'

function invoke_checked {
    param([string]$program, [string[]]$arguments)
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed with exit code $LASTEXITCODE" }
}

if (-not (Test-Path -LiteralPath $task_dependency)) {
    invoke_checked -program git -arguments @('clone', 'https://github.com/skywind3000/kcp.git', $task_dependency)
    invoke_checked -program git -arguments @('-C', $task_dependency, 'checkout', '--detach', $task_commit)
}
$task_head = & git -C $task_dependency rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $task_head -ne $task_commit) { throw 'KCP checkout is not the pinned version' }
$task_changes = & git -C $task_dependency status --porcelain
if ($LASTEXITCODE -ne 0 -or $task_changes) { throw 'KCP checkout has local modifications' }

$task_build = Join-Path $task_root 'build'
invoke_checked -program cmake -arguments @('-S', $task_root, '-B', $task_build, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-Dycnet_build_comparisons=ON')
invoke_checked -program cmake -arguments @('--build', $task_build, '--config', 'Release', '--parallel', '--', '/nodeReuse:false')
invoke_checked -program ctest -arguments @('--test-dir', $task_build, '-C', 'Release', '--output-on-failure')
if (-not $check_only) {
    $task_csv = Join-Path $task_root "docs/$output_name.csv"
    $task_diagnostics = Join-Path $task_root "docs/${output_name}_diagnostics.txt"
    $task_lines = & (Join-Path $task_build 'Release/protocol_comparison.exe') 2> $task_diagnostics
    $task_comparison_exit = $LASTEXITCODE
    if ($task_comparison_exit -notin @(0, 1)) { throw 'Comparison executable failed' }
    [System.IO.File]::WriteAllLines($task_csv, $task_lines, [System.Text.UTF8Encoding]::new($false))
    Write-Output "Results: $task_csv"
    if ($task_comparison_exit -eq 1) { Write-Warning 'Some scenarios have incomplete or invalid delivery; inspect complete/valid columns in the saved CSV' }
}
