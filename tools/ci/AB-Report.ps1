<#
.SYNOPSIS
AB-Bench.ps1이 남긴 jsonl들을 모아 모드별 "head가 base보다 몇 % 빠른가"와 95% 신뢰구간을 Markdown 표로 만든다.
.DESCRIPTION
라운드마다 같은 시점에 잰 base·head 한 쌍의 비율(head/base)을 쓴다(짝지은 비교). 이웃 VM 때문에 머신 전체가
잠깐 느려져도 같은 라운드의 두 값이 함께 느려지므로 비율은 덜 흔들린다.
- 변화율 = exp(median(ln(head/base))) - 1. 95% 구간은 중앙값의 분포 무관 구간(부호 검정 순위)이라 튀는 라운드에 강하다.
- 구간이 0을 포함하면 "구분 불가"다. 구간의 절반 폭이 이번 측정으로 가려낼 수 있는 최소 차이(감지 한계)다.
- 판정: 화면(pixel_hash)이 같고, mean 또는 p95가 유의하게 2% 이상 줄고, 다른 쪽이 유의하게 늘지 않았으면 "개선".
- 기본값으로는 실패시키지 않는다. -FailOnRegressionPct N 이면 유의하게 N% 넘게 느려진 모드가 있을 때 실패한다.
.EXAMPLE
./tools/ci/AB-Report.ps1 -InputPath ab/*.jsonl -SummaryPath $env:GITHUB_STEP_SUMMARY -CommentPath ab/comment.md
#>
param(
    [Parameter(Mandatory)][string[]]$InputPath,
    [string]$SummaryPath = '',
    [string]$CommentPath = '',
    [string]$Title = 'Cloud A/B bench',
    [string]$Context = '',
    [double]$AdoptPct = 2,
    [double]$FailOnRegressionPct = 0
)
$ErrorActionPreference = 'Stop'

function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $n = $sorted.Count
    if ($n % 2 -eq 1) { return $sorted[($n - 1) / 2] }
    return ($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2
}

# 중앙값의 분포 무관 95% 구간에 쓸 순위 k(1부터): P(Binomial(n, 1/2) < k) <= 0.025인 가장 큰 k.
# 구간은 정렬한 값의 k번째 ~ (n+1-k)번째다. n < 6이면 95% 구간을 만들 수 없어 최솟값~최댓값을 쓴다.
function Get-MedianRank([int]$n) {
    $cdf = 0.0
    $coef = 1.0
    $k = 1
    for ($i = 0; $i -le $n; ++$i) {
        if ($i -gt 0) { $coef = $coef * ($n - $i + 1) / $i }
        $cdf += $coef / [math]::Pow(2, $n)
        if ($cdf -gt 0.025) { break }
        $k = $i + 1
    }
    return [math]::Max(1, $k)
}

# 짝지은 ln(head/base)의 중앙값과 95% 구간을 % 변화로 돌려준다.
# 평균 대신 중앙값을 쓰는 이유: 이웃 VM 때문에 한 라운드만 크게 튀는 일이 잦은데, 중앙값은 그런 라운드에 끌려가지 않는다.
function Get-PairedChange([double[]]$baseValues, [double[]]$headValues) {
    $logs = @($(for ($i = 0; $i -lt $baseValues.Count; ++$i) { [math]::Log($headValues[$i] / $baseValues[$i]) }) | Sort-Object)
    $n = $logs.Count
    $k = Get-MedianRank $n
    $pct = { param($v) ([math]::Exp($v) - 1) * 100 }
    $lo = & $pct $logs[$k - 1]
    $hi = & $pct $logs[$n - $k]
    [pscustomobject]@{
        pct    = & $pct (Get-Median $logs)
        lo     = $lo
        hi     = $hi
        half   = ($hi - $lo) / 2
        faster = $hi -lt 0
        slower = $lo -gt 0
    }
}

# 라운드 안 흔들림: base 쪽 값들의 변동계수(표준편차/평균, %).
function Get-Cv([double[]]$values) {
    $avg = ($values | Measure-Object -Average).Average
    $var = 0.0
    foreach ($x in $values) { $var += ($x - $avg) * ($x - $avg) }
    if ($values.Count -lt 2 -or $avg -le 0) { return [double]::NaN }
    return [math]::Sqrt($var / ($values.Count - 1)) / $avg * 100
}

function Format-Change($c) {
    $mark = if ($c.faster) { ' ↓' } elseif ($c.slower) { ' ↑' } else { '' }
    return ('{0:+0.0;-0.0;0.0}% [{1:+0.0;-0.0;0.0}, {2:+0.0;-0.0;0.0}]{3}' -f $c.pct, $c.lo, $c.hi, $mark)
}

$files = @($InputPath | ForEach-Object { Get-ChildItem -Path $_ -File } | Sort-Object FullName -Unique)
if ($files.Count -eq 0) { throw "no A/B results: $($InputPath -join ', ')" }
$rows = @($files | ForEach-Object { Get-Content $_.FullName } | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
$rows = @($rows | Where-Object { [int]$_.round -ge 0 })

$md = [System.Collections.Generic.List[string]]::new()
$md.Add('<!-- sr-ab-bench -->')
$md.Add("### $Title")
$md.Add('')
if ($Context) { $md.Add($Context); $md.Add('') }
$md.Add('| mode | rounds | base mean / p95 ms | head mean / p95 ms | mean 변화 [95% 구간] | p95 변화 [95% 구간] | 감지 한계 (p95) | 흔들림 CV | pixel_hash | 판정 |')
$md.Add('|---|---|---|---|---|---|---|---|---|---|')

$regressions = @()
$machines = @()
$order = @{ fill = 0; wire = 1; normal = 2; aabb = 3; fxaa = 4 }
foreach ($group in ($rows | Group-Object mode | Sort-Object { $order[$_.Name] })) {
    $mode = $group.Name
    # 같은 라운드의 base·head만 짝짓는다(한쪽이 빠진 라운드는 버림).
    $pairs = @($group.Group | Group-Object round | Where-Object {
            @($_.Group | Where-Object side -eq 'base').Count -eq 1 -and @($_.Group | Where-Object side -eq 'head').Count -eq 1
        } | Sort-Object { [int]$_.Name })
    if ($pairs.Count -lt 2) { $md.Add("| $mode | $($pairs.Count) | - | - | - | - | - | - | - | 라운드 부족 |"); continue }
    $b = @($pairs | ForEach-Object { $_.Group | Where-Object side -eq 'base' })
    $h = @($pairs | ForEach-Object { $_.Group | Where-Object side -eq 'head' })
    $bMean = [double[]]@($b | ForEach-Object { [double]$_.mean_ms }); $hMean = [double[]]@($h | ForEach-Object { [double]$_.mean_ms })
    $bP95 = [double[]]@($b | ForEach-Object { [double]$_.p95_ms }); $hP95 = [double[]]@($h | ForEach-Object { [double]$_.p95_ms })
    $meanChange = Get-PairedChange $bMean $hMean
    $p95Change = Get-PairedChange $bP95 $hP95
    $cv = Get-Cv $bP95

    $bHash = @($b | ForEach-Object { $_.pixel_hash } | Select-Object -Unique)
    $hHash = @($h | ForEach-Object { $_.pixel_hash } | Select-Object -Unique)
    $sameHash = $bHash.Count -eq 1 -and $hHash.Count -eq 1 -and $bHash[0] -eq $hHash[0]
    $hashText = if ($sameHash) { "``$($hHash[0])`` 같음" } else { "base ``$($bHash -join ',')`` / head ``$($hHash -join ',')`` **다름**" }

    $improved = ($meanChange.faster -and $meanChange.pct -le -$AdoptPct) -or ($p95Change.faster -and $p95Change.pct -le -$AdoptPct)
    $regressed = $meanChange.slower -or $p95Change.slower
    $verdict = if (-not $sameHash) { '⚠ 화면 바뀜' }
               elseif ($improved -and -not $regressed) { '✅ 개선' }
               elseif ($regressed -and -not $improved) { '❌ 느려짐' }
               elseif ($improved -and $regressed) { '⚖ 엇갈림' }
               else { '➖ 구분 불가' }
    $md.Add(('| {0} | {1} | {2:0.00} / {3:0.00} | {4:0.00} / {5:0.00} | {6} | {7} | ±{8:0.0}% | {9:0.0}% | {10} | {11} |' -f
            $mode, $pairs.Count, (Get-Median $bMean), (Get-Median $bP95), (Get-Median $hMean), (Get-Median $hP95),
            (Format-Change $meanChange), (Format-Change $p95Change), $p95Change.half, $cv, $hashText, $verdict))

    foreach ($c in @($meanChange, $p95Change)) {
        if ($FailOnRegressionPct -gt 0 -and $c.slower -and $c.pct -gt $FailOnRegressionPct) { $regressions += $mode; break }
    }
    $first = $b[0]
    $machines += ('{0}: {1}, {2} logical cores, {3} {4}, {5} threads, {6} frames' -f $mode, $first.cpu, $first.logical_cores, $first.compiler, $first.build, $first.threads, $first.frames)
}

$md.Add('')
$md.Add("변화율은 라운드마다 같은 VM에서 번갈아 잰 head/base 비율의 중앙값이다(음수 = 빨라짐). ↓/↑는 95% 구간이 0을 넘지 않는 유의한 변화.")
$md.Add("✅ 개선 = 화면 같음 + mean 또는 p95가 유의하게 $AdoptPct% 이상 감소 + 유의한 증가 없음. 감지 한계보다 작은 차이는 이 측정으로 가려낼 수 없다.")
$md.Add('클라우드 4 vCPU 수치라 절대값(p95 <= 9ms 판정)은 PC 기준을 따른다. 스레드 수에 따라 달라지는 변경은 PC에서 다르게 나올 수 있다.')
$md.Add('')
$md.Add('<details><summary>측정 환경</summary>')
$md.Add('')
foreach ($m in $machines) { $md.Add("- $m") }
$md.Add('')
$md.Add('</details>')

$text = $md -join "`n"
Write-Output $text
if ($SummaryPath) { Add-Content -Path $SummaryPath -Value $text -Encoding utf8 }
if ($CommentPath) { Set-Content -Path $CommentPath -Value $text -Encoding utf8 }

if ($regressions.Count -gt 0) {
    throw ("significant regression > {0}%: {1}" -f $FailOnRegressionPct, ($regressions -join ', '))
}
