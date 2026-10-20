<#
.SYNOPSIS
Resolve Visual Studio CPU-sampling ETL instruction pointers against the captured EXE/PDB.
.DESCRIPTION
Read-only, two-pass ETL analysis. Reports exclusive instruction-pointer sample counts,
NOT inclusive call-tree CPU or measured CPU milliseconds. Thread shares use samples in
each matching EXE image as their denominator; time spent in TBB/system DLLs is excluded.
Process elapsed time is reported only when real ProcessStart AND ProcessStop events exist.
Rundown/image/sample timestamps are never substituted for process start or stop.
.EXAMPLE
pwsh -File tests/ResolveVsProfile.ps1 -Trace capture.etl -Binary captured/RendererTests.exe -WarmupMs 5000
#>
param(
    [Parameter(Mandatory)][string]$Trace,
    [Parameter(Mandatory)][string]$Binary,
    # ETL 시작 기준이며 기존 > 5000ms 조건과 같다.
    [ValidateRange(0, [double]::MaxValue)][double]$WarmupMs = 5000,
    [ValidateRange(1, 10000)][int]$Top = 40,
    [string]$TraceEventAssembly = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/PrivateAssemblies/Microsoft.Diagnostics.Tracing.TraceEvent.dll'
)
$ErrorActionPreference = 'Stop'
$tracePath = (Resolve-Path -LiteralPath $Trace).Path
$binaryPath = (Resolve-Path -LiteralPath $Binary).Path
$binaryName = [IO.Path]::GetFileName($binaryPath)
$null = [Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $TraceEventAssembly).Path)
if (-not ('VsProfileImageSymbols' -as [type])) { Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VsProfileImageSymbols {
 [DllImport("dbghelp.dll", CharSet=CharSet.Ansi, SetLastError=true)] static extern bool SymInitialize(IntPtr p,string path,bool invade);
 [DllImport("dbghelp.dll", CharSet=CharSet.Ansi, SetLastError=true)] static extern ulong SymLoadModuleEx(IntPtr p,IntPtr file,string image,string module,ulong address,uint size,IntPtr data,uint flags);
 [DllImport("dbghelp.dll", SetLastError=true)] static extern bool SymFromAddr(IntPtr p,ulong address,out ulong displacement,IntPtr info);
 [DllImport("dbghelp.dll")] static extern bool SymCleanup(IntPtr p);
 [DllImport("dbghelp.dll")] static extern uint SymSetOptions(uint options);
 [DllImport("dbghelp.dll")] static extern uint SymGetOptions();
 [DllImport("dbghelp.dll", CharSet=CharSet.Ansi, SetLastError=true)] static extern bool SymGetModuleInfo64(IntPtr p,ulong address,ref ModuleInfo info);
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)] struct ModuleInfo {
  public uint SizeOfStruct; public ulong BaseOfImage;
  public uint ImageSize,TimeDateStamp,CheckSum,NumSyms; public int SymType;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string ModuleName;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=256)] public string ImageName;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=256)] public string LoadedImageName;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=256)] public string LoadedPdbName;
  public uint CVSig;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=780)] public string CVData;
  public uint PdbSig; public Guid PdbSig70; public uint PdbAge;
  [MarshalAs(UnmanagedType.Bool)] public bool PdbUnmatched;
  [MarshalAs(UnmanagedType.Bool)] public bool DbgUnmatched;
  [MarshalAs(UnmanagedType.Bool)] public bool LineNumbers;
  [MarshalAs(UnmanagedType.Bool)] public bool GlobalSymbols;
  [MarshalAs(UnmanagedType.Bool)] public bool TypeInfo;
  [MarshalAs(UnmanagedType.Bool)] public bool SourceIndexed;
  [MarshalAs(UnmanagedType.Bool)] public bool Publics;
  public uint MachineType,Reserved;
 }
 static System.Diagnostics.Process process;
 static ulong symbolBase;
 static uint previousOptions;
 public static string PdbDescription { get; private set; }
 public static void Open(string path) {
  if (IntPtr.Size != 8) throw new Exception("Run symbol analysis in 64-bit PowerShell.");
  process=System.Diagnostics.Process.GetCurrentProcess();
  previousOptions=SymGetOptions();
  // EXACT_SYMBOLS + UNDNAME + FAIL_CRITICAL_ERRORS; never enable LOAD_ANYTHING.
  SymSetOptions(0x400u | 0x2u | 0x200u);
  if (!SymInitialize(process.Handle,System.IO.Path.GetDirectoryName(path),false)) {
   int error=Marshal.GetLastWin32Error(); process.Dispose(); process=null; SymSetOptions(previousOptions);
   throw new Exception("SymInitialize " + error);
  }
  // ASLR 주소와 별개인 심볼 모듈 기준도 DbgHelp의 반환값을 사용한다.
  symbolBase=SymLoadModuleEx(process.Handle,IntPtr.Zero,path,null,0,0,IntPtr.Zero,0);
  if (symbolBase==0) {
   int error=Marshal.GetLastWin32Error(); Close();
   throw new Exception("SymLoadModuleEx " + error);
  }
  var info=new ModuleInfo(); info.SizeOfStruct=(uint)Marshal.SizeOf(typeof(ModuleInfo));
  if (!SymGetModuleInfo64(process.Handle,symbolBase,ref info)) {
   int error=Marshal.GetLastWin32Error(); Close(); throw new Exception("SymGetModuleInfo64 " + error);
  }
  if(info.PdbUnmatched || info.DbgUnmatched || String.IsNullOrEmpty(info.LoadedPdbName)) {
   Close(); throw new Exception("A matching PDB was not loaded. Preserve the captured EXE and matching PDB together.");
  }
  PdbDescription="path="+info.LoadedPdbName+" guid="+info.PdbSig70+" age="+info.PdbAge+" exact_match=true";
 }
 public static string Name(ulong rva) {
  IntPtr buffer=Marshal.AllocHGlobal(88+2048);
  try {
   for(int i=0;i<88+2048;i++) Marshal.WriteByte(buffer,i,0);
   Marshal.WriteInt32(buffer,0,88); Marshal.WriteInt32(buffer,80,2048);
   ulong displacement;
   if(!SymFromAddr(process.Handle,symbolBase+rva,out displacement,buffer)) return "RVA_"+rva.ToString("X");
   return Marshal.PtrToStringAnsi(IntPtr.Add(buffer,84));
  } finally { Marshal.FreeHGlobal(buffer); }
 }
 public static void Close() {
  if(process!=null) { SymCleanup(process.Handle); process.Dispose(); process=null; }
  SymSetOptions(previousOptions);
  symbolBase=0;
 }
}
'@
}

# 종료 rundown만 있는 ETL도 지원하도록 image 발견과 샘플 처리를 분리한다.
# VA뿐 아니라 PID/적재 수명도 검사해 다른 프로세스의 같은 주소를 제외한다.
$images = [Collections.Generic.List[object]]::new()
$activeImages = @{}
$processes = [Collections.Generic.List[object]]::new()
$activeProcesses = @{}
# PID는 재사용될 수 있다. 실제 start마다 별도 lifetime을 만들며 rundown은
# 관찰 기록일 뿐 실제 start/stop 시각으로 승격하지 않는다.
$processStartHandler = [Action[Microsoft.Diagnostics.Tracing.Parsers.Kernel.ProcessTraceData]] {
    param($event)
    $processId = [int]$event.ProcessID
    if ($processId -lt 0) { return }
    if ($activeProcesses.ContainsKey($processId)) {
        $activeProcesses[$processId].BoundEndMs = $event.TimeStampRelativeMSec
    }
    $record = [pscustomobject]@{
        ProcessId = $processId; ImagePath = $event.ImageFileName
        StartMs = [double]$event.TimeStampRelativeMSec; StopMs = $null
        BoundStartMs = [double]$event.TimeStampRelativeMSec; BoundEndMs = [double]::PositiveInfinity
        Discovery = 'ProcessStart'; Instance = $processes.Count
    }
    $processes.Add($record); $activeProcesses[$processId] = $record
}
$processStopHandler = [Action[Microsoft.Diagnostics.Tracing.Parsers.Kernel.ProcessTraceData]] {
    param($event)
    $processId = [int]$event.ProcessID
    if ($processId -lt 0) { return }
    if ($activeProcesses.ContainsKey($processId)) {
        $record = $activeProcesses[$processId]
        $activeProcesses.Remove($processId)
    } else {
        $record = [pscustomobject]@{
            ProcessId = $processId; ImagePath = $event.ImageFileName
            StartMs = $null; StopMs = $null; BoundStartMs = 0.0; BoundEndMs = [double]::PositiveInfinity
            Discovery = 'ProcessStopOnly'; Instance = $processes.Count
        }
        $processes.Add($record)
    }
    $record.StopMs = [double]$event.TimeStampRelativeMSec
    $record.BoundEndMs = [double]$event.TimeStampRelativeMSec
}
$processRundownHandler = [Action[Microsoft.Diagnostics.Tracing.Parsers.Kernel.ProcessTraceData]] {
    param($event)
    $processId = [int]$event.ProcessID
    if ($processId -lt 0 -or $activeProcesses.ContainsKey($processId)) { return }
    $record = [pscustomobject]@{
        ProcessId = $processId; ImagePath = $event.ImageFileName
        StartMs = $null; StopMs = $null; BoundStartMs = 0.0; BoundEndMs = [double]::PositiveInfinity
        Discovery = $event.EventName; Instance = $processes.Count
    }
    $processes.Add($record); $activeProcesses[$processId] = $record
}
$imageHandler = [Action[Microsoft.Diagnostics.Tracing.Parsers.Kernel.ImageLoadTraceData]] {
    param($event)
    if (-not [string]::Equals([IO.Path]::GetFileName($event.FileName), $binaryName,
                             [StringComparison]::OrdinalIgnoreCase)) { return }
    if ($event.ProcessID -lt 0 -or $event.ImageSize -le 0) { return }
    $key = '{0}:{1:X}:{2:X}' -f $event.ProcessID, $event.ImageBase, $event.ImageSize
    $isLoad = $event.EventName -eq 'Image/Load'
    if ($event.EventName -eq 'Image/Unload') {
        if ($activeImages.ContainsKey($key)) {
            $activeImages[$key].EndMs = $event.TimeStampRelativeMSec
            $activeImages.Remove($key)
        }
        return
    }
    if ($activeImages.ContainsKey($key) -and -not $isLoad) { return }
    if ($activeImages.ContainsKey($key)) { $activeImages[$key].EndMs = $event.TimeStampRelativeMSec }
    $record = [pscustomobject]@{
        ProcessId = [int]$event.ProcessID
        Base = [uint64]$event.ImageBase
        Size = [uint64]$event.ImageSize
        StartMs = $(if ($isLoad) { $event.TimeStampRelativeMSec } else { 0.0 })
        EndMs = [double]::PositiveInfinity
        TimeDateStamp = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$event.TimeDateStamp), 0)
        ImagePath = $event.FileName
        Discovery = $event.EventName
        SampleCount = [long]0
        Threads = @{}
        ObservedMs = [double]$event.TimeStampRelativeMSec
        Process = $null
        LifecycleStatus = 'missing_process_events'
        ImageInstance = $images.Count
    }
    $images.Add($record)
    $activeImages[$key] = $record
}
$source = [Microsoft.Diagnostics.Tracing.ETWTraceEventSource]::new($tracePath)
try {
    $source.Kernel.add_ImageGroup($imageHandler)
    $source.Kernel.add_ProcessStart($processStartHandler)
    $source.Kernel.add_ProcessStop($processStopHandler)
    $source.Kernel.add_ProcessDCStart($processRundownHandler)
    $source.Kernel.add_ProcessDCStop($processRundownHandler)
    $null = $source.GetType().GetMethod('Process', [Type[]]@()).Invoke($source, @())
} finally { $source.Dispose() }
if ($images.Count -eq 0) { throw "No image load/rundown event matches '$binaryName' in '$tracePath'." }

# 이름만 같은 다른 빌드의 PDB로 해석하는 실수를 막는다. PE timestamp는
# 재현 빌드에서도 식별 비트로 비교하며 날짜로 해석하지 않는다.
$binaryReader = [IO.BinaryReader]::new([IO.File]::OpenRead($binaryPath))
try {
    $null = $binaryReader.BaseStream.Seek(0x3c, [IO.SeekOrigin]::Begin)
    $peOffset = $binaryReader.ReadInt32()
    $null = $binaryReader.BaseStream.Seek($peOffset, [IO.SeekOrigin]::Begin)
    if ($binaryReader.ReadUInt32() -ne 0x00004550) { throw 'Binary is not a PE image.' }
    $null = $binaryReader.BaseStream.Seek($peOffset + 8, [IO.SeekOrigin]::Begin)
    $binaryStamp = $binaryReader.ReadUInt32()
    $null = $binaryReader.BaseStream.Seek($peOffset + 24 + 56, [IO.SeekOrigin]::Begin)
    $binaryImageSize = $binaryReader.ReadUInt32()
} finally { $binaryReader.Dispose() }
$matchingImages = @($images | Where-Object { $_.TimeDateStamp -eq $binaryStamp -and $_.Size -eq $binaryImageSize })
if ($matchingImages.Count -eq 0) {
    $traceStamps = ($images | ForEach-Object { '0x{0:X8}' -f $_.TimeDateStamp } | Sort-Object -Unique) -join ', '
    throw ('PE timestamp/SizeOfImage mismatch: Binary=0x{0:X8}, size=0x{1:X}; ETL stamps={2}. Use the captured EXE/PDB.' -f $binaryStamp, $binaryImageSize, $traceStamps)
}
if ($matchingImages.Count -ne $images.Count) { Write-Warning 'Excluded other builds with the same image filename.' }
$imagesByProcess = @{}
foreach ($record in $matchingImages) {
    $lifetimes = @($processes | Where-Object {
        $_.ProcessId -eq $record.ProcessId -and $_.BoundStartMs -le $record.ObservedMs -and
        $_.BoundEndMs -ge $record.ObservedMs
    } | Sort-Object BoundStartMs -Descending)
    if ($lifetimes.Count -eq 1) {
        $record.Process = $lifetimes[0]
        $record.LifecycleStatus = 'linked'
        $record.StartMs = [Math]::Max($record.StartMs, $record.Process.BoundStartMs)
        $record.EndMs = [Math]::Min($record.EndMs, $record.Process.BoundEndMs)
    } elseif ($lifetimes.Count -gt 1) {
        $record.LifecycleStatus = 'ambiguous_process_lifetimes'
    }
    if (-not $imagesByProcess.ContainsKey($record.ProcessId)) {
        $imagesByProcess[$record.ProcessId] = [Collections.Generic.List[object]]::new()
    }
    $imagesByProcess[$record.ProcessId].Add($record)
}

$samples = @{}
$counts = @{ Total = [long]0; AfterWarmup = [long]0; TargetProcess = [long]0; MatchedImage = [long]0; UnknownProcess = [long]0 }
$handler = [Action[Microsoft.Diagnostics.Tracing.Parsers.Kernel.SampledProfileTraceData]] {
    param($event)
    $counts.Total++
    if ($event.TimeStampRelativeMSec -le $WarmupMs) { return }
    $counts.AfterWarmup++
    if ($event.ProcessID -lt 0) { $counts.UnknownProcess++; return }
    if (-not $imagesByProcess.ContainsKey([int]$event.ProcessID)) { return }
    $address = [uint64]$event.InstructionPointer
    $countedProcess = $false
    foreach ($record in $imagesByProcess[[int]$event.ProcessID]) {
        if ($event.TimeStampRelativeMSec -lt $record.StartMs -or $event.TimeStampRelativeMSec -ge $record.EndMs) { continue }
        if (-not $countedProcess) { $counts.TargetProcess++; $countedProcess = $true }
        if ($address -lt $record.Base -or $address - $record.Base -ge $record.Size) { continue }
        $rva = [uint64]($address - $record.Base)
        if ($samples.ContainsKey($rva)) { $samples[$rva]++ } else { $samples[$rva] = [long]1 }
        $record.SampleCount++
        $threadId = [int]$event.ThreadID
        if ($record.Threads.ContainsKey($threadId)) { $record.Threads[$threadId]++ }
        else { $record.Threads[$threadId] = [long]1 }
        $counts.MatchedImage++
        break
    }
}
$source = [Microsoft.Diagnostics.Tracing.ETWTraceEventSource]::new($tracePath)
try {
    $source.Kernel.add_PerfInfoSample($handler)
    $null = $source.GetType().GetMethod('Process', [Type[]]@()).Invoke($source, @())
} finally { $source.Dispose() }
'TRACE {0}' -f $tracePath
'BINARY {0}' -f $binaryPath
'METRIC exclusive_instruction_pointer_samples; NOT inclusive_cpu, CPU milliseconds, or elapsed time'
'THREAD_SCOPE matched_EXE_image_only; TBB/system DLL samples excluded; tid=-1 means missing thread identity'
'WARMUP_ORIGIN ETL_start; process elapsed includes startup/warmup/shutdown and is not render-only duration'
'COUNTS warmup_ms={0} all_samples={1} after_warmup={2} target_pid_samples={3} image_samples={4} unknown_pid_after_warmup={5} unique_rvas={6}' -f $WarmupMs, $counts.Total, $counts.AfterWarmup, $counts.TargetProcess, $counts.MatchedImage, $counts.UnknownProcess, $samples.Count
foreach ($record in $matchingImages) {
    'IMAGE instance={0} pid={1} base=0x{2:X} size=0x{3:X} filter_start_ms={4:F3} filter_end_ms={5} self_samples={6} source={7}' -f $record.ImageInstance, $record.ProcessId, $record.Base, $record.Size, $record.StartMs, $record.EndMs, $record.SampleCount, $record.Discovery
    $process = $record.Process
    $start = 'missing'; $stop = 'missing'; $elapsed = 'missing'; $reason = $record.LifecycleStatus
    if ($null -ne $process) {
        if ($null -ne $process.StartMs) { $start = '{0:F3}' -f $process.StartMs }
        if ($null -ne $process.StopMs) { $stop = '{0:F3}' -f $process.StopMs }
        if ($null -ne $process.StartMs -and $null -ne $process.StopMs -and $process.StopMs -ge $process.StartMs) {
            $elapsed = '{0:F3}' -f ($process.StopMs - $process.StartMs)
            $reason = 'actual_ProcessStart_and_ProcessStop'
        } else { $reason = 'missing_actual_ProcessStart_or_ProcessStop' }
    }
    'PROCESS image_instance={0} pid={1} actual_start_ms={2} actual_stop_ms={3} elapsed_ms={4} lifecycle={5}' -f $record.ImageInstance, $record.ProcessId, $start, $stop, $elapsed, $reason
    $knownThreads = @($record.Threads.GetEnumerator() | Where-Object { $_.Key -ge 0 } | Sort-Object Value -Descending)
    $busiestShare = if ($knownThreads.Count -gt 0 -and $record.SampleCount -gt 0) {
        100.0 * $knownThreads[0].Value / $record.SampleCount
    } else { 0.0 }
    $unknownThreadSamples = if ($record.Threads.ContainsKey(-1)) { $record.Threads[-1] } else { 0 }
    'THREAD_SUMMARY image_instance={0} sampled_known_tids={1} busiest_image_share_pct={2:F3} unknown_tid_self_samples={3}' -f $record.ImageInstance, $knownThreads.Count, $busiestShare, $unknownThreadSamples
    foreach ($thread in @($record.Threads.GetEnumerator() | Sort-Object Value -Descending)) {
        $share = if ($record.SampleCount -gt 0) { 100.0 * $thread.Value / $record.SampleCount } else { 0.0 }
        'THREAD image_instance={0} pid={1} tid={2} image_self_samples={3} image_share_pct={4:F3}' -f $record.ImageInstance, $record.ProcessId, $thread.Key, $thread.Value, $share
    }
    if ($record.Threads.Count -eq 0) { 'THREAD image_instance={0} distribution=missing_no_image_samples' -f $record.ImageInstance }
}
if ($samples.Count -eq 0) { Write-Warning 'No image samples remained after warmup/PID/address filters.'; return }
$byName = @{}
[VsProfileImageSymbols]::Open($binaryPath)
try {
    'PDB {0}' -f [VsProfileImageSymbols]::PdbDescription
    foreach ($sample in $samples.GetEnumerator()) {
        $name = [VsProfileImageSymbols]::Name([uint64]$sample.Key)
        $byName[$name] += $sample.Value
    }
    "SELF_IP_SAMPLES`tFUNCTION"
    $byName.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $Top | ForEach-Object { "$($_.Value)`t$($_.Key)" }
} finally { [VsProfileImageSymbols]::Close() }
