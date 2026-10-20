#requires -Version 7.2
<#
.SYNOPSIS
CPU 수집 구간에 시작된 빌드 프로세스를 제한된 메타데이터로 기록한다.
.DESCRIPTION
Dot-source해서 Start/Receive/Stop-BuildInterferenceMonitor를 호출한다. 파일을
읽는 것만으로는 구독하거나 프로세스를 실행하지 않는다. 호출자는 반드시 finally에서
Stop을 호출하며, target.WaitForExit(1000) 사이에 Receive를 호출한다.
외부 프로세스를 종료하거나 명령행을 읽지 않는다. 시작 이벤트가 없다는 결과는
시스템 전체의 idle 증명이 아니다. 같은 guard를 baseline/candidate 모두에 적용한다.
#>

function Start-BuildInterferenceMonitor {
    [CmdletBinding()]
    param([ValidateRange(1, 16384)][int]$MaximumEvents = 4096)

    $monitor = [pscustomobject]@{
        SourceIdentifier = 'SoftwareRenderer.BuildInterference.' + [Guid]::NewGuid().ToString('N')
        StartedUtc = [DateTime]::UtcNow
        EndedUtc = $null
        Registered = $false
        Stopped = $false
        MaximumEvents = $MaximumEvents
        DroppedEvents = 0L
        Events = [Collections.Generic.List[object]]::new()
        Errors = [Collections.Generic.List[string]]::new()
        ResidentProcesses = [Collections.Generic.List[object]]::new()
        ResidentWarnings = [Collections.Generic.List[object]]::new()
        Report = $null
    }
    # 시작 추적만 구독한다. IDE 이름만으로 빌드 여부를 추정하지 않으며,
    # profiler/renderer 자체는 이 필터에 포함하지 않는다.
    $buildNames = @('MSBuild.exe', 'cl.exe', 'link.exe', 'ninja.exe', 'nmake.exe',
        'cmake.exe', 'clang-cl.exe', 'clang.exe', 'clang++.exe', 'gcc.exe', 'g++.exe',
        'lld-link.exe', 'ld.exe', 'csc.exe', 'vbc.exe', 'rc.exe', 'mt.exe')
    $conditions = ($buildNames | ForEach-Object { "ProcessName = '$_'" }) -join ' OR '
    try {
        if (-not $IsWindows) { throw 'Windows is required.' }
        $null = Register-CimIndicationEvent -Namespace 'root/cimv2' `
            -Query "SELECT * FROM Win32_ProcessStartTrace WHERE $conditions" `
            -SourceIdentifier $monitor.SourceIdentifier -OperationTimeoutSec 10 -ErrorAction Stop
        $monitor.Registered = $true
    } catch {
        # 메시지 원문에 환경별 경로/명령행이 섞이지 않도록 예외 형식만 저장한다.
        $reason = 'subscription_failed:' + $_.Exception.GetType().Name
        if ($null -ne $_.Exception.PSObject.Properties['NativeErrorCode']) {
            $reason += ':native_code=' + $_.Exception.NativeErrorCode
        }
        $monitor.Errors.Add($reason)
        Unregister-Event -SourceIdentifier $monitor.SourceIdentifier -ErrorAction SilentlyContinue
    }

    # 이미 실행 중인 MSBuild는 시작 이벤트가 없을 수 있다. PID 재사용을 피하려고
    # 시작 시각과 handle을 보존한다. CPU 차이는 경고용이며 validity 판정에 쓰지 않는다.
    foreach ($process in @(Get-Process -Name 'MSBuild' -ErrorAction SilentlyContinue)) {
        try {
            $null = $process.Handle
            $monitor.ResidentProcesses.Add([pscustomobject]@{
                Process = $process
                ProcessId = $process.Id
                StartedUtc = $process.StartTime.ToUniversalTime()
                CpuMilliseconds = $process.TotalProcessorTime.TotalMilliseconds
            })
        } catch {
            $monitor.ResidentWarnings.Add(@{ pid = $process.Id; reason = 'initial_cpu_sample_unavailable' })
            $process.Dispose()
        }
    }
    return $monitor
}

function Receive-BuildInterferenceEvents {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][psobject]$Monitor,
        # Stop이 자기 구독을 해제한 직후 이미 큐에 도착한 항목도 보존한다.
        [switch]$SubscriptionClosed
    )

    if ($Monitor.Stopped -or -not $Monitor.Registered) { return }
    try {
        if (-not $SubscriptionClosed -and
            $null -eq (Get-EventSubscriber -SourceIdentifier $Monitor.SourceIdentifier -ErrorAction SilentlyContinue)) {
            if (-not $Monitor.Errors.Contains('subscription_lost')) { $Monitor.Errors.Add('subscription_lost') }
            return
        }
        foreach ($queuedEvent in @(Get-Event -SourceIdentifier $Monitor.SourceIdentifier -ErrorAction SilentlyContinue)) {
            try {
                $started = $queuedEvent.SourceEventArgs.NewEvent
                $eventUtc = [DateTime]::FromFileTimeUtc([long]$started.TIME_CREATED)
                if ($eventUtc -lt $Monitor.StartedUtc -or
                    ($null -ne $Monitor.EndedUtc -and $eventUtc -gt $Monitor.EndedUtc)) { continue }
                if ($Monitor.Events.Count -ge $Monitor.MaximumEvents) {
                    $Monitor.DroppedEvents++
                    continue
                }
                # process 종료/PID 재사용 후에도 이벤트가 가진 네 필드만 저장한다.
                # CommandLine이나 전체 CIM 객체는 결과에 포함하지 않는다.
                $Monitor.Events.Add([pscustomobject]@{
                    pid = [uint32]$started.ProcessID
                    parent_pid = [uint32]$started.ParentProcessID
                    created_utc = $eventUtc.ToString('o')
                    name = [string]$started.ProcessName
                })
            } catch {
                if (-not $Monitor.Errors.Contains('event_decode_failed')) { $Monitor.Errors.Add('event_decode_failed') }
            } finally {
                Remove-Event -EventIdentifier $queuedEvent.EventIdentifier -ErrorAction SilentlyContinue
            }
        }
    } catch {
        if (-not $Monitor.Errors.Contains('event_drain_failed')) { $Monitor.Errors.Add('event_drain_failed') }
    }
}

function Stop-BuildInterferenceMonitor {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][psobject]$Monitor,
        [DateTime]$ObservationEndUtc = [DateTime]::UtcNow,
        [ValidateRange(0, 5000)][int]$FinalDrainMilliseconds = 500
    )

    if ($Monitor.Stopped) { return $Monitor.Report }
    $Monitor.EndedUtc = $ObservationEndUtc.ToUniversalTime()
    try {
        # 종료 전에 발생했지만 늦게 도착한 이벤트를 유한 시간만 더 기다린다.
        # 종료 이후 시작한 프로세스는 TIME_CREATED로 제외한다.
        $drainClock = [Diagnostics.Stopwatch]::StartNew()
        do {
            Receive-BuildInterferenceEvents -Monitor $Monitor
            $remaining = $FinalDrainMilliseconds - $drainClock.ElapsedMilliseconds
            if ($remaining -gt 0 -and $Monitor.Registered) {
                Start-Sleep -Milliseconds ([int][Math]::Min(50, $remaining))
            } else { break }
        } while ($true)

        foreach ($resident in $Monitor.ResidentProcesses) {
            try {
                $resident.Process.Refresh()
                $delta = $resident.Process.TotalProcessorTime.TotalMilliseconds - $resident.CpuMilliseconds
                if ($delta -gt 0) {
                    $Monitor.ResidentWarnings.Add(@{
                        pid = $resident.ProcessId
                        started_utc = $resident.StartedUtc.ToString('o')
                        cpu_delta_ms = $delta
                        reason = 'resident_msbuild_cpu_increased; advisory only, not proof of a build'
                    })
                }
            } catch {
                $Monitor.ResidentWarnings.Add(@{ pid = $resident.ProcessId; reason = 'final_cpu_sample_unavailable' })
            }
        }
    } finally {
        # 이번 GUID 구독/큐만 정리한다. 외부 구독이나 프로세스에는 손대지 않는다.
        Receive-BuildInterferenceEvents -Monitor $Monitor
        if ($null -ne (Get-EventSubscriber -SourceIdentifier $Monitor.SourceIdentifier -ErrorAction SilentlyContinue)) {
            try { Unregister-Event -SourceIdentifier $Monitor.SourceIdentifier -ErrorAction Stop }
            catch { $Monitor.Errors.Add('subscription_cleanup_failed') }
        }
        Receive-BuildInterferenceEvents -Monitor $Monitor -SubscriptionClosed
        foreach ($resident in $Monitor.ResidentProcesses) { $resident.Process.Dispose() }
        $Monitor.ResidentProcesses.Clear()
        $Monitor.Stopped = $true
    }

    # 이미 drain한 항목도 caller가 지정한 실제 target 종료 시각으로 다시 한정한다.
    $eventsInWindow = @($Monitor.Events.ToArray() | Where-Object {
        [DateTime]::Parse($_.created_utc).ToUniversalTime() -le $Monitor.EndedUtc
    })
    $complete = $Monitor.Registered -and $Monitor.Errors.Count -eq 0
    $interference = $eventsInWindow.Count -gt 0 -or $Monitor.DroppedEvents -gt 0
    $status = if ($interference) { 'invalid_build_start_observed' }
              elseif (-not $complete) { 'unverified' }
              else { 'no_build_start_observed' }
    $Monitor.Report = [pscustomobject]@{
        status = $status
        valid_for_comparison = $complete -and -not $interference
        monitoring_available = $Monitor.Registered
        observation_complete = $complete
        started_utc = $Monitor.StartedUtc.ToString('o')
        ended_utc = $Monitor.EndedUtc.ToString('o')
        build_starts = $eventsInWindow
        dropped_event_count = $Monitor.DroppedEvents
        errors = @($Monitor.Errors.ToArray())
        resident_msbuild_warnings = @($Monitor.ResidentWarnings.ToArray())
        policy = 'Build-start observation only; no events or zero resident CPU delta do not prove idleness. Preserve artifacts for invalid/unverified runs. Final drain is bounded; delivery beyond it is not guaranteed.'
    }
    return $Monitor.Report
}
