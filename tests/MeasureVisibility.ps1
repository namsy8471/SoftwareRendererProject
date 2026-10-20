param(
    [ValidateRange(1, 100)][int]$Runs = 5,
    [ValidateRange(1, 100000)][int]$Frames = 600,
    [ValidateSet('real', 'occluded', 'open')][string]$Scenario = 'real',
    [ValidateSet('fill', 'normal', 'wire', 'fxaa')][string[]]$Modes = @('fill', 'normal', 'wire'),
    [ValidateSet('none', 'sort', 'hiz', 'both')][string[]]$Options = @('none'),
    [ValidateRange(1, 4096)][int]$Width = 1280,
    [ValidateRange(1, 2160)][int]$Height = 720,
    [switch]$CompareBaseline,
    [switch]$HiddenWireframe
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$current = Join-Path $PSScriptRoot 'bin/x64/Release/RendererTests.exe'
$baseline = Join-Path $PSScriptRoot 'bin/profiles/before-visibility/RendererTests.exe'
if (-not (Test-Path -LiteralPath $current)) { throw 'Build Release x64 first.' }
if ($CompareBaseline -and ($Scenario -ne 'real' -or $Options.Count -ne 1 -or $Options[0] -ne 'none')) {
    throw 'The historical binary supports only real scenes without visibility options.'
}
if ($CompareBaseline -and -not (Test-Path -LiteralPath $baseline)) {
    throw 'The local historical binary is unavailable; do not substitute a different baseline.'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$wirePolicy = if ($HiddenWireframe) { 'hidden' } else { 'xray' }
$output = Join-Path $PSScriptRoot "bin/profiles/visibility-$Scenario-${Width}x${Height}-$stamp-$wirePolicy"
New-Item -ItemType Directory -Path $output | Out-Null
# 측정은 순차 실행한다. 빌드/프로파일러와 동시에 실행하지 않는다.
# before/after 순서를 교대로 뒤집어 실행 순서의 편향을 줄인다.
for ($run = 1; $run -le $Runs; ++$run) {
    foreach ($mode in $Modes) {
        foreach ($option in $Options) {
            $versions = @('after')
            if ($CompareBaseline) {
                $versions = if ($run % 2) { @('before', 'after') } else { @('after', 'before') }
            }
            foreach ($version in $versions) {
                $exe = if ($version -eq 'before') { $baseline } else { $current }
                $arguments = @($projectRoot, '--benchmark', $Frames, $Width, $Height, $mode)
                if ($Scenario -ne 'real') {
                    $arguments[1] = '--visibility-benchmark'
                    $arguments += @($option, $Scenario)
                } elseif ($version -eq 'after') {
                    $arguments += $option
                }
                if ($HiddenWireframe -and $version -eq 'after') {
                    $arguments += @('nostats', 'hidden')
                }
                $log = Join-Path $output "$version-$mode-$option-$run.txt"
                Write-Output "Run $run/$Runs $version $mode $option ${Width}x${Height}"
                & $exe @arguments | Tee-Object -FilePath $log
                if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $log" }
            }
        }
    }
}
Write-Output "Raw results: $output"
