<#
.SYNOPSIS
솔루션(앱, RendererTests, srcli)을 빌드하고 RendererTests를 실행한다.
.EXAMPLE
./tests/RunTests.ps1 -Quick                       # x64 Release만, AI 반복 작업용
./tests/RunTests.ps1 -Quick -Filter golden        # 이름에 golden이 들어간 테스트만
./tests/RunTests.ps1 -Quick -UpdateGolden         # 의도한 화면 변경 후 골든 PNG 갱신
./tests/RunTests.ps1 -Rebuild                     # 네 구성 전체 재빌드(커밋 전 확인)
./tests/RunTests.ps1 -Quick -MSBuildArgs '/p:SrAnalyze=true'   # MSVC /analyze 정적 분석을 켜고 빌드
.NOTES
출력은 구성마다 BUILD/PASS/FAIL 줄과 테스트 SUMMARY 줄이다. 빌드 실패 시 첫 오류 줄만 보여 주고
전체 로그 경로를 알려 준다(tests/bin/logs).
#>
param(
    [ValidateSet('Debug', 'Release')][string[]]$Configurations = @('Debug', 'Release'),
    [ValidateSet('x64', 'Win32')][string[]]$Platforms = @('x64', 'Win32'),
    [switch]$Rebuild,
    [switch]$Quick,
    [string]$Filter = '',
    [switch]$UpdateGolden,
    # CI 품질 검사용 추가 MSBuild 인수. 예: '/p:SrAnalyze=true'(/analyze), '/p:EnableASAN=true'
    [string[]]$MSBuildArgs = @()
)
if ($Quick) {
    $Configurations = @('Release')
    $Platforms = @('x64')
}
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio C++ tools were not found.' }
$buildTool = Join-Path $visualStudio 'MSBuild/Current/Bin/MSBuild.exe'
$toolsVersion = (Get-Content (Join-Path $visualStudio 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$originalPath = $env:PATH
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$logs = Join-Path $PSScriptRoot 'bin/logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
try {
    foreach ($platform in $Platforms) {
        foreach ($configuration in $Configurations) {
            $solutionPlatform = if ($platform -eq 'Win32') { 'x86' } else { 'x64' }
            $runtimeArch = if ($platform -eq 'Win32') { 'x86' } else { 'x64' }
            $env:PATH = (Join-Path $visualStudio "VC/Tools/MSVC/$toolsVersion/bin/Hostx64/$runtimeArch") + ";$originalPath"
            # 프로파일 수집 전 빌드 worker가 불필요하게 상주하지 않도록 한다.
            $buildLog = "$logs/$platform-$configuration-build.log"
            $null = & $buildTool (Join-Path $projectRoot 'SoftrendererProject.sln') /m /nr:false /nologo /v:quiet "/t:$target" "/p:Configuration=$configuration" "/p:Platform=$solutionPlatform" "/flp:logfile=$buildLog;verbosity=normal" @MSBuildArgs
            if ($LASTEXITCODE -ne 0) {
                Select-String -Path $buildLog -Pattern ': (fatal )?error ' | Select-Object -First 10 | ForEach-Object { Write-Output $_.Line.Trim() }
                throw "Build failed: $platform $configuration (log: $buildLog)"
            }
            Write-Output "BUILD OK: $platform $configuration"
            $output = Join-Path $PSScriptRoot "bin/$platform/$configuration"
            $testArguments = @($projectRoot)
            if ($Filter) { $testArguments += @('--filter', $Filter) }
            if ($UpdateGolden) { $testArguments += '--update-golden' }
            & (Join-Path $output 'RendererTests.exe') @testArguments | Tee-Object -FilePath "$logs/$platform-$configuration-tests.log" | Where-Object { $_ -notmatch '^PASS ' }
            if ($LASTEXITCODE -ne 0) { throw "Tests failed: $platform $configuration" }
            Write-Output "PASS: $platform $configuration"
        }
    }
}
finally {
    $env:PATH = $originalPath
}
