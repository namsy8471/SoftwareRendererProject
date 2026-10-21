<#
.SYNOPSIS
같은 머신에서 기준(base) srcli와 변경(head) srcli를 번갈아 실행해 한 모드의 A/B 측정값을 JSON Lines로 남긴다.
.DESCRIPTION
클라우드 runner는 절대 수치가 PC와 다르고 이웃 VM 때문에 흔들리지만, 같은 VM에서 두 빌드를 번갈아 재면
그 흔들림이 양쪽에 비슷하게 들어가서 "몇 % 빨라졌나"는 비교할 수 있다.
- 라운드마다 순서를 바꾼다(0: base→head, 1: head→base, ...). 시간에 따라 느려지는/빨라지는 추세가 한쪽에만 쌓이지 않게 하기 위해서다.
- 맨 앞 한 쌍은 예열(캐시·클럭 안정)로 기록만 하고 분석에서 뺀다(round = -1).
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
    [ValidateRange(2, 100)][int]$Rounds = 10,
    [ValidateRange(1, 100000)][int]$Frames = 600,
    [ValidateRange(0, 100000)][int]$Warmup = 60,
    [string]$Size = '1920x1080',
    [string]$BaseLabel = 'base',
    [string]$HeadLabel = 'head',
    [string[]]$ExtraArgs = @()
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
    Write-Output ('round {0,2} {1,-4} mean {2,8:0.000} ms  p95 {3,8:0.000} ms  hash {4}' -f $round, $side, [double]$row.mean_ms, [double]$row.p95_ms, $row.pixel_hash)
}

Write-Output "A/B $Mode $Size, $Rounds rounds x $Frames frames (warmup $Warmup), $cpu, $cores logical cores"
for ($round = -1; $round -lt $Rounds; ++$round) {
    $order = if ($round % 2 -eq 0) { @('base', 'head') } else { @('head', 'base') }
    foreach ($side in $order) { Invoke-Side $side $round }
}
Write-Output "saved: $Out"
