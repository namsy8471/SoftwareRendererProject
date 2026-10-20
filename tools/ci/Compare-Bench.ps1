<#
.SYNOPSIS
tests/Bench.ps1이 남긴 jsonl을 모드별 중앙값으로 묶어 Markdown 표로 만들고, 기준(main) jsonl이 있으면 전후 차이를 붙인다.
.DESCRIPTION
CI 요약(GITHUB_STEP_SUMMARY)용이다. 기본값으로는 절대 기준(p95 <= 9ms)도 회귀도 실패로 만들지 않는다.
측정이 안정되면 -FailOnRegressionPct 로 "main보다 p95가 N% 넘게 느려지면 실패"를 켠다.
pixel_hash가 기준과 다르면 화면이 바뀐 것이므로 표에 표시한다(성능 변경은 화면을 바꾸면 안 된다).
.EXAMPLE
./tools/ci/Compare-Bench.ps1 -Current tests/out/bench/x.jsonl -Baseline baseline/bench.jsonl -SummaryPath $env:GITHUB_STEP_SUMMARY
#>
param(
    [Parameter(Mandatory)][string]$Current,
    [string]$Baseline = '',
    [double]$P95BudgetMs = 9,
    [double]$FailOnRegressionPct = 0,
    [string]$SummaryPath = ''
)
$ErrorActionPreference = 'Stop'

function Get-Median([double[]]$values) {
    $sorted = $values | Sort-Object
    return $sorted[[int][math]::Floor(($sorted.Count - 1) / 2)]
}

# 모드별 {mean, p95, hash} 중앙값. 같은 모드의 여러 실행(-Runs N)을 하나로 묶는다.
function Read-Bench([string]$path) {
    $rows = @{}
    $lines = Get-Content $path | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json }
    foreach ($group in ($lines | Group-Object mode)) {
        $rows[$group.Name] = [pscustomobject]@{
            mean = Get-Median @($group.Group | ForEach-Object { [double]$_.mean_ms })
            p95  = Get-Median @($group.Group | ForEach-Object { [double]$_.p95_ms })
            hash = (@($group.Group | ForEach-Object { $_.pixel_hash } | Select-Object -Unique) -join ',')
            size = '{0}x{1}' -f $group.Group[0].width, $group.Group[0].height
            runs = $group.Count
        }
    }
    return $rows
}

function Format-Delta([double]$now, [double]$before) {
    if ($before -le 0) { return '' }
    $pct = ($now - $before) / $before * 100
    return ('{0:+0.0;-0.0;0.0}%' -f $pct)
}

$cur = Read-Bench $Current
$base = if ($Baseline -and (Test-Path $Baseline)) { Read-Bench $Baseline } else { $null }

$md = [System.Collections.Generic.List[string]]::new()
$md.Add('### Benchmark (Windows PC, MSVC Release x64)')
$md.Add('')
if ($base) {
    $md.Add('| mode | size | runs | mean ms | p95 ms | p95 <= {0} | main p95 | p95 변화 | mean 변화 | pixel_hash |' -f $P95BudgetMs)
    $md.Add('|---|---|---|---|---|---|---|---|---|---|')
} else {
    $md.Add('| mode | size | runs | mean ms | p95 ms | p95 <= {0} | pixel_hash |' -f $P95BudgetMs)
    $md.Add('|---|---|---|---|---|---|---|')
}

$regressions = @()
foreach ($mode in ($cur.Keys | Sort-Object)) {
    $c = $cur[$mode]
    $met = if ($c.p95 -le $P95BudgetMs) { 'OK' } else { 'NOT MET' }
    if ($base -and $base.ContainsKey($mode)) {
        $b = $base[$mode]
        $hash = if ($b.hash -eq $c.hash) { "``$($c.hash)`` (같음)" } else { "``$($c.hash)`` (main ``$($b.hash)``, **다름**)" }
        $md.Add(('| {0} | {1} | {2} | {3:0.000} | {4:0.000} | {5} | {6:0.000} | {7} | {8} | {9} |' -f
                $mode, $c.size, $c.runs, $c.mean, $c.p95, $met, $b.p95, (Format-Delta $c.p95 $b.p95), (Format-Delta $c.mean $b.mean), $hash))
        if ($FailOnRegressionPct -gt 0 -and $b.p95 -gt 0 -and (($c.p95 - $b.p95) / $b.p95 * 100) -gt $FailOnRegressionPct) {
            $regressions += $mode
        }
    } elseif ($base) {
        $md.Add(('| {0} | {1} | {2} | {3:0.000} | {4:0.000} | {5} | - | - | - | `{6}` |' -f $mode, $c.size, $c.runs, $c.mean, $c.p95, $met, $c.hash))
    } else {
        $md.Add(('| {0} | {1} | {2} | {3:0.000} | {4:0.000} | {5} | `{6}` |' -f $mode, $c.size, $c.runs, $c.mean, $c.p95, $met, $c.hash))
    }
}
$md.Add('')
$md.Add($(if ($base) { '기준: main 브랜치의 마지막 성공한 실행.' } else { '기준(main) 결과가 없어 이번 수치만 표시한다.' }))
$md.Add('같은 PC라도 다른 작업이 돌고 있으면 수치가 흔들린다. 지금은 참고용이며 절대 기준 미달로 실패시키지 않는다.')

$text = $md -join "`n"
Write-Output $text
if ($SummaryPath) { Add-Content -Path $SummaryPath -Value $text -Encoding utf8 }

if ($regressions.Count -gt 0) {
    throw ("p95 regression > {0}% vs main: {1}" -f $FailOnRegressionPct, ($regressions -join ', '))
}
