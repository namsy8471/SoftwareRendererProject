#requires -Version 7.2
<#
.SYNOPSIS
보존한 Release 실행 파일로 VS CPU Usage / VTune software hotspots를 순차 수집한다.
.DESCRIPTION
빌드는 하지 않는다. 다른 빌드/벤치마크/IDE 프로파일 세션을 먼저 끝낸 뒤 실행한다.
새 출력 폴더만 허용하며 EXE/PDB/DLL/assets와 SHA256을 보존한다. 종료 대상은 이번에 시작한
GUID 세션과 고유 snapshot 경로의 프로세스뿐이다. 외부 세션을 조회/중단하지 않는다.
BENCH stdout은 정상 완료/해시 확인용 원본 로그일 뿐, chrono 성능 수치를 해석하지 않는다.
성능 근거는 VS ETL 분석과 VTune 보고서다. -Analyze는 수집을 모두 마친 뒤 실행한다.
빌드 시작 이벤트를 구독하지만 실행 자체를 차단하지는 않는다. 감지된 capture는 보존하되
비교에서 제외한다. 감시 권한이 없는 환경은 기본 거절하며, -AllowUnverifiedBuildMonitor를
명시하면 raw 수집/분석만 허용하고 valid_for_comparison=false와 경고를 남긴다.
.EXAMPLE
pwsh -File tests/CaptureCpuProfiles.ps1 -OutputDirectory tests/bin/profiles/cpu-before-001 -Analyze
.EXAMPLE
pwsh -File tests/CaptureCpuProfiles.ps1 -OutputDirectory tests/bin/profiles/cpu-hidden-001 -Modes wire -HiddenWireframe -Analyze
#>
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Binary = (Join-Path $PSScriptRoot 'bin/x64/Release/RendererTests.exe'),
    [string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent),
    [ValidateSet('fill', 'normal', 'wire')][string[]]$Modes = @('fill', 'normal', 'wire'),
    [ValidateRange(1, 100000)][int]$Frames = 1200,
    [ValidateRange(1, 4096)][int]$Width = 1280,
    [ValidateRange(1, 2160)][int]$Height = 720,
    [switch]$HiddenWireframe,
    [switch]$Analyze,
    [switch]$AllowUnverifiedBuildMonitor,
    [ValidateRange(5, 600)][int]$StartupTimeoutSeconds = 120,
    [string]$VsDiagnostics = 'C:/Program Files/Microsoft Visual Studio/18/Community/Team Tools/DiagnosticsHub/Collector/VSDiagnostics.exe',
    [string]$VsConfig = 'C:/Program Files/Microsoft Visual Studio/18/Community/Team Tools/DiagnosticsHub/Collector/AgentConfigs/CpuUsageBase.json',
    [string]$VTune = 'C:/Program Files (x86)/Intel/oneAPI/vtune/latest/bin64/vtune.exe',
    [string]$TraceEventAssembly = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/PrivateAssemblies/Microsoft.Diagnostics.Tracing.TraceEvent.dll'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows -or [IntPtr]::Size -ne 8) { throw 'Use 64-bit PowerShell 7.2+ on Windows.' }
if ($Modes.Count -eq 0 -or @($Modes | Select-Object -Unique).Count -ne $Modes.Count) {
    throw 'Modes must be a nonempty list without duplicates.'
}
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
$pdbPath = [IO.Path]::ChangeExtension($binaryPath, '.pdb')
if (-not (Test-Path -LiteralPath $pdbPath -PathType Leaf)) { throw "Matching PDB is required: $pdbPath" }
$assetSource = Join-Path (Split-Path $binaryPath -Parent) 'assets'
if (-not (Test-Path -LiteralPath $assetSource -PathType Container)) {
    throw "The renderer loads assets relative to its EXE. Deploy the matching assets before capture: $assetSource"
}
$assetSource = (Resolve-Path -LiteralPath $assetSource).Path
$projectPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$vsPath = (Resolve-Path -LiteralPath $VsDiagnostics).Path
$configPath = (Resolve-Path -LiteralPath $VsConfig).Path
$vtunePath = (Resolve-Path -LiteralPath $VTune).Path
$analysisScript = Join-Path $PSScriptRoot 'ResolveVsProfile.ps1'
$monitorScript = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'BuildInterferenceMonitor.ps1')).Path
. $monitorScript
if ($Analyze) {
    $null = Resolve-Path -LiteralPath $analysisScript
    $assemblyPath = (Resolve-Path -LiteralPath $TraceEventAssembly).Path
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputPath) { throw "Output must not exist; nothing will be overwritten: $outputPath" }
$binaryProcessName = [IO.Path]::GetFileNameWithoutExtension($binaryPath)
$residentBuildWarnings = [Collections.Generic.List[object]]::new()
$reportedResidentBuildPids = [Collections.Generic.HashSet[int]]::new()
$buildMonitorReports = [Collections.Generic.List[object]]::new()
$captures = [Collections.Generic.List[object]]::new()
$manifest = $null
$captureRunCompleted = $false

function Assert-IdleMachine {
    # 같은 이름의 다른 빌드도 보수적으로 거절한다. 외부 프로세스를 종료하지 않는다.
    $conflicts = @(Get-Process -Name @($binaryProcessName, 'MSBuild', 'cl', 'link', 'ninja',
        'VSDiagnostics', 'VSPerfCmd', 'vsperf', 'vtune', 'vtune-gui', 'amplxe-cl',
        'amplxe-runss', 'amplxe-gui') -ErrorAction SilentlyContinue)
    $blocking = [Collections.Generic.List[object]]::new()
    foreach ($process in $conflicts) {
        $resident = $false
        if ($process.ProcessName -ieq 'MSBuild') {
            # 빌드 완료 뒤 남는 node-reuse worker만 예외로 한다. CIM 조회 실패/
            # 불명확한 명령행은 안전하게 차단하며 부모 MSBuild, cl, link는 계속 차단한다.
            $details = Get-CimInstance Win32_Process -Filter "ProcessId = $($process.Id)" -ErrorAction SilentlyContinue
            if ($null -ne $details -and -not [string]::IsNullOrWhiteSpace($details.CommandLine)) {
                $commandLine = [string]$details.CommandLine
                $resident = $commandLine -match '(?i)(?:^|\s)[/-]nodemode:1(?=\s|$)' -and
                            $commandLine -match '(?i)(?:^|\s)[/-]nodereuse:true(?=\s|$)'
                if ($resident -and $reportedResidentBuildPids.Add($process.Id)) {
                    $residentBuildWarnings.Add(@{ pid = $process.Id; command_line = $commandLine
                        reason = 'Explicit /nodemode:1 /nodeReuse:true resident worker; idleness is NOT proven.' })
                    Write-Warning "Skipping reusable MSBuild worker pid=$($process.Id). This does NOT prove idleness; keep exclusive machine use."
                }
            }
        }
        if (-not $resident) { $blocking.Add($process) }
    }
    if ($blocking.Count -gt 0) {
        $description = ($blocking | ForEach-Object { '{0}(pid={1})' -f $_.ProcessName, $_.Id }) -join ', '
        throw "Build/profiler/target process is already running. Leave it untouched and retry when idle: $description"
    }
}

# Start-Process의 ArgumentList는 문자열로 다시 합쳐진다. Win32 quoting을 한 번만 적용한다.
function ConvertTo-WindowsArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $quoted = [Text.StringBuilder]::new('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') {
            $null = $quoted.Append(('\' * ($slashes * 2 + 1))).Append('"')
        } else {
            $null = $quoted.Append(('\' * $slashes)).Append($character)
        }
        $slashes = 0
    }
    $null = $quoted.Append(('\' * ($slashes * 2))).Append('"')
    return $quoted.ToString()
}
function Join-WindowsArguments([string[]]$Values) {
    return (($Values | ForEach-Object { ConvertTo-WindowsArgument $_ }) -join ' ')
}
function Start-LoggedProcess([string]$File, [string[]]$Arguments, [string]$LogPrefix) {
    foreach ($suffix in @('.stdout.txt', '.stderr.txt', '.command.json')) {
        if (Test-Path -LiteralPath ($LogPrefix + $suffix)) { throw "Refusing existing log: $LogPrefix$suffix" }
    }
    @{ executable = $File; arguments = $Arguments; working_directory = $projectPath } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($LogPrefix + '.command.json') -Encoding utf8
    return Start-Process -FilePath $File -ArgumentList (Join-WindowsArguments $Arguments) `
        -WorkingDirectory $projectPath -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput ($LogPrefix + '.stdout.txt') -RedirectStandardError ($LogPrefix + '.stderr.txt')
}
function Invoke-LoggedProcess([string]$File, [string[]]$Arguments, [string]$LogPrefix) {
    $process = Start-LoggedProcess $File $Arguments $LogPrefix
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "Command failed (exit=$($process.ExitCode)): $LogPrefix" }
    } finally { $process.Dispose() }
}
function Invoke-ProfileLaunch([string]$File, [string[]]$Arguments, [string]$LogPrefix,
                              [string]$TargetPath) {
    # launcher가 반환한 뒤 Get-Process 한 번만 실행하면 짧은 작업을 놓친다.
    # launcher 실행 중부터 고유 snapshot의 정확한 Path+StartTime을 확인하고 Handle을 잡는다.
    $monitorLog = $LogPrefix + '.interference.json'
    if (Test-Path -LiteralPath $monitorLog) { throw "Refusing existing monitor report: $monitorLog" }
    $monitor = Start-BuildInterferenceMonitor
    $launcher = $null
    $target = $null
    $completed = $false
    $observationEndUtc = $null
    $launchResult = $null
    try {
        if (-not $monitor.Registered) {
            if (-not $AllowUnverifiedBuildMonitor) {
                $failure = [InvalidOperationException]::new(
                    "Build-start monitoring is unavailable; no profiler was launched. Report: $monitorLog. Use -AllowUnverifiedBuildMonitor only for explicitly unverified raw captures.")
                $failure.Data['BuildMonitorPreventedLaunch'] = $true
                throw $failure
            }
            Write-Warning 'Build-start monitoring is unavailable. Explicit override permits raw capture only; valid_for_comparison remains false.'
        }
        $launchTime = [DateTime]::UtcNow
        $launcher = Start-LoggedProcess $File $Arguments $LogPrefix
        $deadline = $launchTime.AddSeconds($StartupTimeoutSeconds)
        $nextDrainUtc = $launchTime
        while ($null -eq $target) {
            if ([DateTime]::UtcNow -ge $nextDrainUtc) {
                Receive-BuildInterferenceEvents $monitor
                $nextDrainUtc = [DateTime]::UtcNow.AddSeconds(1)
            }
            foreach ($candidate in @(Get-Process -Name $binaryProcessName -ErrorAction SilentlyContinue)) {
                try {
                    if ([string]::Equals($candidate.Path, $TargetPath, [StringComparison]::OrdinalIgnoreCase) -and
                        $candidate.StartTime.ToUniversalTime() -ge $launchTime) {
                        $null = $candidate.Handle
                        if ($null -ne $target) { throw 'Multiple processes unexpectedly use this unique snapshot path.' }
                        $target = $candidate
                        continue
                    }
                } catch [InvalidOperationException] { }
                  catch [ComponentModel.Win32Exception] { }
                $candidate.Dispose()
            }
            if ($null -ne $target) { break }
            if ($launcher.HasExited -and $launcher.ExitCode -ne 0) {
                throw "Profiler launch failed (exit=$($launcher.ExitCode)): $LogPrefix"
            }
            if ([DateTime]::UtcNow -ge $deadline) {
                throw "Could not acquire the exact launched target process; no successful capture is claimed: $LogPrefix"
            }
            Start-Sleep -Milliseconds 20
        }
        $targetId = $target.Id
        while (-not $target.WaitForExit(1000)) { Receive-BuildInterferenceEvents $monitor }
        $observationEndUtc = $target.ExitTime.ToUniversalTime()
        $monitor.EndedUtc = $observationEndUtc
        Receive-BuildInterferenceEvents $monitor
        $targetExit = $target.ExitCode
        while (-not $launcher.WaitForExit(1000)) { Receive-BuildInterferenceEvents $monitor }
        if ($targetExit -ne 0 -or $launcher.ExitCode -ne 0) {
            throw "Abnormal target/profiler exit: target pid=$targetId exit=$targetExit, launcher exit=$($launcher.ExitCode); $LogPrefix"
        }
        $completed = $true
        $launchResult = @{ target_pid = $targetId; target_exit = $targetExit; launcher_exit = $launcher.ExitCode }
    } finally {
        try {
            # 실패/취소 때도 외부 이름 기반 kill은 하지 않는다. 획득한 이번 프로세스만 정리한다.
            if (-not $completed) {
                if ($null -ne $target -and -not $target.HasExited) { $target.Kill(); $target.WaitForExit() }
                if ($null -ne $launcher -and -not $launcher.HasExited) { $launcher.Kill($true); $launcher.WaitForExit() }
            }
        } finally {
            if ($null -eq $observationEndUtc) { $observationEndUtc = [DateTime]::UtcNow }
            try {
                $monitorReport = Stop-BuildInterferenceMonitor $monitor -ObservationEndUtc $observationEndUtc
                $buildMonitorReports.Add(@{ log = $monitorLog; report = $monitorReport })
                $monitorReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $monitorLog -Encoding utf8
                if ($null -ne $launchResult) {
                    $launchResult.build_monitor = $monitorReport
                    $launchResult.build_monitor_log = $monitorLog
                }
            } finally {
                if ($null -ne $target) { $target.Dispose() }
                if ($null -ne $launcher) { $launcher.Dispose() }
            }
        }
    }
    return $launchResult
}

function Assert-CaptureBuildMonitor([object]$Launch) {
    $report = $Launch.build_monitor
    if ($report.status -eq 'invalid_build_start_observed') {
        throw "Build start detected during capture. Artifacts are preserved but invalid; no further capture will run: $($Launch.build_monitor_log)"
    }
    if (-not $report.valid_for_comparison) {
        if (-not $AllowUnverifiedBuildMonitor) {
            throw "Build-start observation is incomplete. Artifacts are preserved but unverified: $($Launch.build_monitor_log)"
        }
        Write-Warning "Explicit override: unverified raw capture/analysis only; not a verified comparison: $($Launch.build_monitor_log)"
    }
}

$captureMutex = [Threading.Mutex]::new($false, 'Local\SoftwareRendererProject.CpuCapture')
$ownsMutex = $false
try {
    try { $ownsMutex = $captureMutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { throw 'Another CaptureCpuProfiles.ps1 run owns the capture lock.' }
    Assert-IdleMachine
    # -Force 없이 생성하므로 사전 검사 뒤 다른 작업이 같은 폴더를 만들었어도 실패한다.
    $null = New-Item -ItemType Directory -Path $outputPath
    $snapshotPath = Join-Path $outputPath 'snapshot'
    $null = New-Item -ItemType Directory -Path $snapshotPath
    $sourceFiles = @((Get-Item -LiteralPath $binaryPath), (Get-Item -LiteralPath $pdbPath)) +
        @(Get-ChildItem -LiteralPath (Split-Path $binaryPath -Parent) -File -Filter '*.dll')
    $hashes = @()
    foreach ($file in $sourceFiles) {
        $hashBefore = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $destination = Join-Path $snapshotPath $file.Name
        [IO.File]::Copy($file.FullName, $destination, $false)
        $hashAfter = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $copiedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($hashBefore -ne $hashAfter -or $hashBefore -ne $copiedHash) { throw "Binary changed during snapshot: $($file.FullName)" }
        $hashes += @{ source = $file.FullName; snapshot = $destination; sha256 = $copiedHash }
    }
    # Utils::MakeAssetPath는 EXE/assets를 읽는다. 프로젝트 자산으로 조용히
    # 대체하지 않고 실제 배포 자산을 그대로 복사하여 각 파일의 SHA256을 남긴다.
    $assetSnapshot = Join-Path $snapshotPath 'assets'
    $null = New-Item -ItemType Directory -Path $assetSnapshot
    $assetEntries = @(Get-ChildItem -LiteralPath $assetSource -Recurse -Force)
    if (((Get-Item -LiteralPath $assetSource).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        @($assetEntries | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }).Count -gt 0) {
        throw 'Asset reparse points are unsupported; provide a self-contained deployed assets directory.'
    }
    foreach ($file in @($assetEntries | Where-Object { -not $_.PSIsContainer })) {
        $relative = [IO.Path]::GetRelativePath($assetSource, $file.FullName)
        $destination = Join-Path $assetSnapshot $relative
        $directory = Split-Path $destination -Parent
        if (-not (Test-Path -LiteralPath $directory)) { $null = New-Item -ItemType Directory -Path $directory }
        $hashBefore = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        [IO.File]::Copy($file.FullName, $destination, $false)
        $hashAfter = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $copiedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($hashBefore -ne $hashAfter -or $hashBefore -ne $copiedHash) { throw "Asset changed during snapshot: $($file.FullName)" }
        $hashes += @{ source = $file.FullName; snapshot = $destination; sha256 = $copiedHash; kind = 'asset' }
    }
    $snapshotBinary = Join-Path $snapshotPath ([IO.Path]::GetFileName($binaryPath))
    $manifest = @{
        created_utc = [DateTime]::UtcNow.ToString('o'); project_root = $projectPath
        frames = $Frames; width = $Width; height = $Height; modes = $Modes
        hidden_wireframe = [bool]$HiddenWireframe; optimization = 'none'; instrumentation = 'nostats'
        vs_tool = $vsPath; vs_config = $configPath; vtune_tool = $vtunePath; files = $hashes
        asset_source = $assetSource; asset_policy = 'Exact deployed EXE/assets snapshot; no repository fallback.'
        resident_msbuild_warnings = @($residentBuildWarnings.ToArray())
        build_monitor_policy = 'Win32_ProcessStartTrace build names only; same guard required for both sides. No events do not prove system idleness.'
        build_monitor_guard = @{ version = 1
            helper_sha256 = (Get-FileHash -LiteralPath $monitorScript -Algorithm SHA256).Hash
            capture_script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash }
        allow_unverified_build_monitor = [bool]$AllowUnverifiedBuildMonitor
        valid_for_comparison = $false; capture_status = 'in_progress'
        metric_policy = 'Use only VS/VTune reports. BENCH chrono values are raw logs, not evidence.'
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding utf8
    foreach ($mode in $Modes) {
        $workload = @($projectPath, '--benchmark', "$Frames", "$Width", "$Height", $mode, 'none', 'nostats')
        if ($HiddenWireframe) { $workload += 'hidden' }
        Assert-IdleMachine
        $session = [Guid]::NewGuid().ToString('D')
        $vsPrefix = Join-Path $outputPath "vs-$mode"
        $package = $vsPrefix + '.diagsession'
        $session | Set-Content -LiteralPath ($vsPrefix + '.session.txt') -Encoding utf8
        $sessionAttempted = $false
        $sessionStopped = $false
        try {
            Write-Host "VS CpuUsageBase: $mode (session $session)"
            $sessionAttempted = $true
            $launch = Invoke-ProfileLaunch $vsPath @('start', $session, "/launch:$snapshotBinary",
                "/launchArgs:$(Join-WindowsArguments $workload)", "/loadConfig:$configPath") ($vsPrefix + '-start') $snapshotBinary
            # stop에는 package 저장이 포함된다. 이번 GUID만 지정하며 외부 세션에 손대지 않는다.
            Invoke-LoggedProcess $vsPath @('stop', $session, "/output:$package") ($vsPrefix + '-stop')
            $sessionStopped = $true
            if (-not (Test-Path -LiteralPath $package -PathType Leaf)) { throw "VS package is missing: $package" }
            Invoke-LoggedProcess $vsPath @('expandDiagSession', $package) ($vsPrefix + '-expand')
            $etls = @(Get-ChildItem -LiteralPath $vsPrefix -File -Recurse -Filter '*.etl')
            if ($etls.Count -eq 0) { throw "Expanded VS package contains no ETL: $vsPrefix" }
            $captures.Add(@{ mode = $mode; tool = 'VS'; session = $session; process = $launch
                package = $package; etls = @($etls.FullName)
                valid_for_comparison = $launch.build_monitor.valid_for_comparison })
            Assert-CaptureBuildMonitor $launch
        } catch {
            if ($_.Exception.Data.Contains('BuildMonitorPreventedLaunch')) { $sessionAttempted = $false }
            throw
        } finally {
            if ($sessionAttempted -and -not $sessionStopped) {
                # 부분 결과와 원래 로그는 보존한다. 기존 package를 덮어쓰지 않는 별도 복구 경로다.
                try {
                    Invoke-LoggedProcess $vsPath @('stop', $session, "/output:$vsPrefix-recovery.diagsession") ($vsPrefix + '-recovery-stop')
                } catch { Write-Warning "Own session $session cleanup failed; manual recovery may be needed: $_" }
            }
        }
        Assert-IdleMachine
        Write-Host "VTune software hotspots: $mode"
        $vtuneResult = Join-Path $outputPath "vtune-$mode"
        $launch = Invoke-ProfileLaunch $vtunePath (@('-collect', 'hotspots', '-knob', 'sampling-mode=sw',
            '-knob', 'enable-characterization-insights=false', '-result-dir', $vtuneResult, '--', $snapshotBinary) +
            $workload) (Join-Path $outputPath "vtune-$mode-collect") $snapshotBinary
        $captures.Add(@{ mode = $mode; tool = 'VTune'; process = $launch; result = $vtuneResult
            valid_for_comparison = $launch.build_monitor.valid_for_comparison })
        Assert-CaptureBuildMonitor $launch
    }
    $captures | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'captures.json') -Encoding utf8
    if ($Analyze) {
        # 분석과 수집도 겹치지 않는다. VS 전체 workload와 startup 이후 thread 분포를 따로 출력한다.
        foreach ($capture in $captures) {
            if ($capture.tool -eq 'VS') {
                $index = 0
                foreach ($etl in $capture.etls) {
                    foreach ($warmup in @(0, 5000)) {
                        $prefix = Join-Path $outputPath "vs-$($capture.mode)-etl$index-warmup$warmup-analysis"
                        Invoke-LoggedProcess (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile', '-File', $analysisScript,
                            '-Trace', $etl, '-Binary', $snapshotBinary, '-WarmupMs', "$warmup",
                            '-TraceEventAssembly', $assemblyPath) $prefix
                    }
                    $index++
                }
            } else {
                foreach ($report in @('summary', 'hotspots', 'threads')) {
                    $arguments = @('-report', $(if ($report -eq 'threads') { 'hotspots' } else { $report }),
                                   '-result-dir', $capture.result)
                    if ($report -eq 'threads') { $arguments += @('-group-by', 'thread') }
                    Invoke-LoggedProcess $vtunePath $arguments (Join-Path $outputPath "vtune-$($capture.mode)-$report-report")
                }
            }
        }
    }
    Write-Host "CPU profiler artifacts: $outputPath"
    Write-Host 'Compare VS/VTune reports only; this script does not interpret BENCH timing values.'
    $captureRunCompleted = $true
} finally {
    try {
        # 수집/구독 실패도 로그를 남긴다. invalid run을 성공 run처럼 재사용하지 않는다.
        if ($null -ne $manifest) {
            $manifest.build_monitor_runs = @($buildMonitorReports.ToArray())
            $manifest.capture_status = if ($captureRunCompleted) { 'completed' } else { 'aborted' }
            $invalid = @($buildMonitorReports | Where-Object { $_.report.status -eq 'invalid_build_start_observed' }).Count -gt 0
            $unverified = $buildMonitorReports.Count -eq 0 -or
                @($buildMonitorReports | Where-Object { -not $_.report.observation_complete }).Count -gt 0
            $manifest.build_monitor_status = if ($invalid) { 'invalid_build_start_observed' }
                elseif ($unverified) { 'unverified' } else { 'no_build_start_observed' }
            $manifest.valid_for_comparison = $captureRunCompleted -and -not $invalid -and -not $unverified
            $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding utf8
            ConvertTo-Json -InputObject @($captures.ToArray()) -Depth 10 |
                Set-Content -LiteralPath (Join-Path $outputPath 'captures.json') -Encoding utf8
        }
    } finally {
        if ($ownsMutex) { $captureMutex.ReleaseMutex() }
        $captureMutex.Dispose()
    }
}
