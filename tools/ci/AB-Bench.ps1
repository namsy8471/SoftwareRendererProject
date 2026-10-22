<#
.SYNOPSIS
같은 머신에서 기준(base) srcli와 변경(head) srcli를 번갈아 실행해 한 모드의 A/B 측정값을 JSON Lines로 남긴다.
.DESCRIPTION
클라우드 runner는 절대 수치가 PC와 다르고 이웃 VM 때문에 흔들리지만, 같은 VM에서 두 빌드를 번갈아 재면
그 흔들림이 양쪽에 비슷하게 들어가서 "몇 % 빨라졌나"는 비교할 수 있다.
- 라운드마다 순서를 바꾼다(0: base→head, 1: head→base, ...). 시간에 따라 느려지는/빨라지는 추세가 한쪽에만 쌓이지 않게 하기 위해서다.
- 맨 앞 한 쌍은 예열(캐시·클럭 안정)로 기록만 하고 분석에서 뺀다(round = -1).
- base·head 실행 폴더(exe, dll, assets)가 바이트까지 같으면 측정하지 않는다(-Force로 강제 가능).
- 조기 종료: -MinRounds(8)부터 -CheckEvery(4)라운드마다 결과가 이미 분명한지 보고, 분명하면 -Rounds(최대 30) 전에 멈춘다.
  "2% 이상 바뀜"은 99% 구간이 0을 넘지 않고 중앙값이 2% 이상일 때, "2% 이상 바뀌지 않음"은 95% 구간 전체가 ±2% 안일 때만
  분명하다고 본다(mean, p95 둘 다 분명해야 멈춘다). 중간에 여러 번 들여다보면 우연히 맞는 경우가 늘어나므로 "바뀜" 쪽은
  최종 판정(95%)보다 엄격한 99%를 쓴다.
  화면(pixel_hash)이 다르면 첫 확인에서 멈춘다. 최종 판정 규칙은 AB-Report.ps1 그대로이고, -NoEarlyStop이면 -Rounds까지 다 잰다.
- 분석·표 작성은 tools/ci/AB-Report.ps1이 한다. Windows·Linux 둘 다 동작한다.
.EXAMPLE
./tools/ci/AB-Bench.ps1 -Base ../base/tools/bin/x64/Release/srcli.exe -Head tools/bin/x64/Release/srcli.exe -Mode normal -Out ab/normal.jsonl
./tools/ci/AB-Bench.ps1 -Base /tmp/base/out/release/srcli -Head out/release/srcli -Mode fill -Rounds 4 -Out ab/fill.jsonl
#>
param(
    [Parameter(Mandatory)][string]$Base,
    [Parameter(Mandatory)][string]$Head,
    [ValidateSet('fill', 'wire', 'normal', 'aabb', 'fxaa')][Parameter(Mandatory)][string]$Mode,
    [Parameter(Mandatory)][string]$Out,
    [ValidateRange(2, 200)][int]$Rounds = 30,
    [ValidateRange(2, 200)][int]$MinRounds = 8,
    [ValidateRange(1, 200)][int]$CheckEvery = 4,
    [double]$AdoptPct = 2,
    [switch]$NoEarlyStop,
    [ValidateRange(1, 100000)][int]$Frames = 200,
    [ValidateRange(0, 100000)][int]$Warmup = 60,
    [string]$Size = '1920x1080',
    [string]$BaseLabel = 'base',
    [string]$HeadLabel = 'head',
    [string[]]$ExtraArgs = @(),
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
foreach ($exe in @($Base, $Head)) {
    if (-not (Test-Path $exe)) { throw "srcli not found: $exe" }
}

function Get-CpuName {
    try {
        if ($IsWindows) { return (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim() }
        $line = Get-Content /proc/cpuinfo | Where-Object { $_ -like 'model name*' } | Select-Object -First 1
        return ($line -split ':', 2)[1].Trim()
    } catch { return 'unknown' }
}

# 실행에 쓰이는 폴더 전체(exe, dll, assets)의 내용 해시. 상대 경로와 파일 해시를 함께 묶는다.
function Get-BundleHash([string]$exe) {
    $root = (Resolve-Path (Split-Path $exe -Parent)).Path
    $lines = Get-ChildItem $root -Recurse -File | Where-Object { $_.Extension -notin '.pdb', '.ilk', '.exp', '.lib' } |
        Sort-Object FullName | ForEach-Object {
            $rel = [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
            '{0} {1}' -f $rel, (Get-FileHash $_.FullName -Algorithm SHA256).Hash
        }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
}

# 중앙값의 분포 무관 구간에 쓸 순위 k(1부터): P(Binomial(n, 1/2) < k) <= alpha/2인 가장 큰 k. AB-Report.ps1과 같은 방식.
function Get-MedianRank([int]$n, [double]$alpha) {
    $cdf = 0.0
    $coef = 1.0
    $k = 1
    for ($i = 0; $i -le $n; ++$i) {
        if ($i -gt 0) { $coef = $coef * ($n - $i + 1) / $i }
        $cdf += $coef / [math]::Pow(2, $n)
        if ($cdf -gt $alpha / 2) { break }
        $k = $i + 1
    }
    return [math]::Max(1, $k)
}

# 짝지은 ln(head/base)의 중앙값 구간과 중앙값을 % 변화로 돌려준다(lo, hi, median).
function Get-Interval([double[]]$baseValues, [double[]]$headValues, [double]$alpha) {
    $logs = @($(for ($i = 0; $i -lt $baseValues.Count; ++$i) { [math]::Log($headValues[$i] / $baseValues[$i]) }) | Sort-Object)
    $k = Get-MedianRank $logs.Count $alpha
    $n = $logs.Count
    $median = ($logs[[math]::Floor(($n - 1) / 2)] + $logs[[math]::Floor($n / 2)]) / 2
    return @((([math]::Exp($logs[$k - 1]) - 1) * 100), (([math]::Exp($logs[$n - $k]) - 1) * 100), (([math]::Exp($median) - 1) * 100))
}

# 지금까지 잰 라운드로 결과가 이미 분명하면 멈출 이유를 돌려준다. 아직 모르면 빈 문자열.
function Get-StopReason {
    $rows = @($script:measured | Where-Object { [int]$_.round -ge 0 })
    $b = @($rows | Where-Object side -eq 'base' | Sort-Object { [int]$_.round })
    $h = @($rows | Where-Object side -eq 'head' | Sort-Object { [int]$_.round })
    if (@($rows | ForEach-Object { $_.pixel_hash } | Select-Object -Unique).Count -gt 1) { return 'pixel_hash differs' }
    $states = foreach ($metric in @('mean_ms', 'p95_ms')) {
        $bv = [double[]]@($b | ForEach-Object { [double]$_.$metric })
        $hv = [double[]]@($h | ForEach-Object { [double]$_.$metric })
        $c99 = Get-Interval $bv $hv 0.01
        $c95 = Get-Interval $bv $hv 0.05
        if (($c99[1] -lt 0 -and $c99[2] -le -$AdoptPct) -or ($c99[0] -gt 0 -and $c99[2] -ge $AdoptPct)) { 'changed' }
        elseif ($c95[0] -gt -$AdoptPct -and $c95[1] -lt $AdoptPct) { 'same' }
        else { 'unclear' }
    }
    if ($states -contains 'unclear') { return '' }
    return "clear after $($b.Count) rounds (mean: $($states[0]), p95: $($states[1]))"
}

$cpu = Get-CpuName
$cores = [Environment]::ProcessorCount
$outDir = Split-Path $Out -Parent
if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
Set-Content -Path $Out -Value $null -NoNewline

function Invoke-Side([string]$side, [int]$round) {
    $exe = if ($side -eq 'base') { $Base } else { $Head }
    $label = if ($side -eq 'base') { $BaseLabel } else { $HeadLabel }
    $line = & $exe bench --mode $Mode --size $Size --frames $Frames --warmup $Warmup --json @ExtraArgs
    if ($LASTEXITCODE -ne 0) { throw "srcli ($side) failed for mode $Mode" }
    $row = $line | ConvertFrom-Json
    $row | Add-Member side $side
    $row | Add-Member label $label
    $row | Add-Member round $round
    $row | Add-Member cpu $cpu
    $row | Add-Member logical_cores $cores
    Add-Content -Path $Out -Value ($row | ConvertTo-Json -Compress)
    $script:measured.Add($row)
    Write-Output ('round {0,2} {1,-4} mean {2,8:0.000} ms  p95 {3,8:0.000} ms  hash {4}' -f $round, $side, [double]$row.mean_ms, [double]$row.p95_ms, $row.pixel_hash)
}

# 같은 소스를 같은 경로에서 /Brepro로 빌드하면 바이트까지 같은 결과가 나온다. 그때는 재 봐야 잡음뿐이므로
# 측정 대신 "동일" 한 줄만 남긴다(AB-Report.ps1이 "변화 없음"으로 표시). -Force면 그래도 잰다(A/A 잡음 확인용).
$baseHash = Get-BundleHash $Base
$headHash = Get-BundleHash $Head
Write-Output "bundle sha256: base $baseHash, head $headHash"
if ($baseHash -eq $headHash -and -not $Force) {
    $row = [pscustomobject]@{ mode = $Mode; identical = $true; bundle_sha256 = $headHash; cpu = $cpu; logical_cores = $cores; round = 0 }
    Add-Content -Path $Out -Value ($row | ConvertTo-Json -Compress)
    Write-Output "base and head builds are byte-identical: skipped measurement"
    Write-Output "saved: $Out"
    return
}

$measured = [System.Collections.Generic.List[object]]::new()
$stopNote = if ($NoEarlyStop) { 'no early stop' } else { "early stop checks from $MinRounds every $CheckEvery" }
Write-Output "A/B $Mode $Size, up to $Rounds rounds x $Frames frames (warmup $Warmup, $stopNote), $cpu, $cores logical cores"
for ($round = -1; $round -lt $Rounds; ++$round) {
    $order = if ($round % 2 -eq 0) { @('base', 'head') } else { @('head', 'base') }
    foreach ($side in $order) { Invoke-Side $side $round }
    $done = $round + 1
    if (-not $NoEarlyStop -and $done -ge $MinRounds -and $done -lt $Rounds -and ($done - $MinRounds) % $CheckEvery -eq 0) {
        $reason = Get-StopReason
        if ($reason) { Write-Output "early stop: $reason"; break }
    }
}
Write-Output "saved: $Out"
