<#
.SYNOPSIS
srcli로 데모 장면의 프레임 시간을 모드별로 측정해 표와 JSON Lines로 남긴다.
.DESCRIPTION
먼저 ./tests/RunTests.ps1 -Quick 으로 x64 Release를 빌드한다. 한 프레임 = 장면 갱신 + Clear + RenderScene
(GDI 표시 제외)이며, 목표는 모든 기본 모드에서 mean fps >= 100이다.
판정은 모든 모드의 p95 <= 9ms(표시 여유 1ms)이며, -Runs N이면 각 모드를 N번 실행해 중앙값을 쓴다.
결과는 tests/out/bench/<시각>[-라벨].jsonl에 한 줄씩 저장되어 변경 전후 비교에 쓴다.
.EXAMPLE
./tests/Bench.ps1 -Runs 3                          # fill, wire, normal / 1920x1080 / 600 frames, 3회 중앙값
./tests/Bench.ps1 -Modes fill -Frames 1200 -Label before-tile-change
#>
param(
    [ValidateSet('fill', 'wire', 'normal', 'aabb', 'fxaa')][string[]]$Modes = @('fill', 'wire', 'normal'),
    [ValidateRange(1, 100000)][int]$Frames = 600,
    [string]$Size = '1920x1080',
    [double]$TargetFps = 100,
    [double]$P95BudgetMs = 9,
    [ValidateRange(1, 20)][int]$Runs = 1,
    [string]$Label = '',
    [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $projectRoot 'tools/bin/x64/Release/srcli.exe'
if (-not (Test-Path $cli)) { throw "srcli not found: $cli (build first: ./tests/RunTests.ps1 -Quick)" }
$outDir = Join-Path $projectRoot 'tests/out/bench'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$name = if ($Label) { "$stamp-$Label" } else { $stamp }
$outFile = Join-Path $outDir "$name.jsonl"

function Get-Median([double[]]$values) {
    $sorted = $values | Sort-Object
    return $sorted[[int][math]::Floor(($sorted.Count - 1) / 2)]
}

$rows = foreach ($mode in $Modes) {
    $results = for ($run = 0; $run -lt $Runs; ++$run) {
        $line = & $cli bench --mode $mode --frames $Frames --size $Size --target-fps $TargetFps --json @ExtraArgs
        if ($LASTEXITCODE -ne 0) { throw "srcli failed for mode $mode" }
        Add-Content -Path $outFile -Value $line
        $line | ConvertFrom-Json
    }
    $hashes = @($results | ForEach-Object { $_.pixel_hash } | Select-Object -Unique)
    $p95 = Get-Median @($results | ForEach-Object { [double]$_.p95_ms })
    [pscustomobject]@{
        mode    = $mode
        runs    = $Runs
        mean_ms = [math]::Round((Get-Median @($results | ForEach-Object { [double]$_.mean_ms })), 3)
        p95_ms  = [math]::Round($p95, 3)
        fps     = [math]::Round(1000.0 / (Get-Median @($results | ForEach-Object { [double]$_.mean_ms })), 1)
        met     = $p95 -le $P95BudgetMs
        hash    = ($hashes -join ',')
    }
}
$rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Output
$failed = @($rows | Where-Object { -not $_.met })
Write-Output ("goal p95 <= {0} ms: {1}" -f $P95BudgetMs, $(if ($failed.Count -eq 0) { 'MET' } else { 'NOT MET (' + (($failed | ForEach-Object { $_.mode }) -join ', ') + ')' }))
Write-Output "saved: $outFile"
