<#
.SYNOPSIS
작업 흐름(스레드)마다 독립된 git worktree를 만들고 지운다.
.DESCRIPTION
worktree는 메인 체크아웃 옆의 ..\SR-worktrees\<이름> 에 만든다. <이름>은 브랜치 이름에서 '/'를 '-'로 바꾼 것이다.
oneTBB의 .lib/.dll/.pdb는 .gitignore(*.lib, *.dll) 때문에 저장소에 없으므로, 새 worktree에는
메인 체크아웃의 src/ThirdParty/OneTBB_2023.1.0/{lib,redist}에서 빠진 파일만 복사한다.
빌드 산출물(tests/bin, tools/bin, x64 등)은 worktree마다 따로 생기므로 서로 덮어쓰지 않는다.
.EXAMPLE
./tools/Worktree.ps1 new perf/normal-shading               # origin/main에서 새 브랜치 + worktree
./tools/Worktree.ps1 new perf/normal-shading -Base ai-restructure-and-perf
./tools/Worktree.ps1 new ci-quality-and-pc-runner          # 이미 있는 브랜치를 worktree로 연다
./tools/Worktree.ps1 deps                                  # 현재 worktree에 oneTBB 바이너리만 다시 채운다
./tools/Worktree.ps1 list
./tools/Worktree.ps1 remove perf/normal-shading            # 변경이 남아 있으면 실패한다(-Force로 강제)
#>
param(
    [Parameter(Mandatory, Position = 0)][ValidateSet('new', 'remove', 'deps', 'list')][string]$Command,
    [Parameter(Position = 1)][string]$Branch = '',
    [string]$Base = 'origin/main',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

# git-common-dir의 부모가 메인 체크아웃이다. 어느 worktree에서 실행해도 같은 값을 준다.
$commonDir = (git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir).Trim()
$mainRoot = Split-Path $commonDir -Parent
$worktreeRoot = Join-Path (Split-Path $mainRoot -Parent) 'SR-worktrees'
$tbbRelative = 'src/ThirdParty/OneTBB_2023.1.0'

function Get-WorktreePath([string]$name) {
    if (-not $name) { throw 'Branch name is required.' }
    Join-Path $worktreeRoot ($name -replace '[\\/]', '-')
}

function Copy-VendoredBinaries([string]$destinationRoot) {
    foreach ($folder in @('lib', 'redist')) {
        $source = Join-Path $mainRoot "$tbbRelative/$folder"
        $destination = Join-Path $destinationRoot "$tbbRelative/$folder"
        if ((Resolve-Path $source).Path -eq (Resolve-Path -LiteralPath $destination -ErrorAction SilentlyContinue)?.Path) { continue }
        # /XC /XN /XO: 이미 있는 파일은 건드리지 않고 빠진 파일만 채운다.
        $null = robocopy $source $destination *.lib *.dll *.pdb /E /XC /XN /XO /NFL /NDL /NJH /NJS /NP
        if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE): $source -> $destination" }
    }
    $probe = Join-Path $destinationRoot "$tbbRelative/lib/intel64/vc14/tbb12.lib"
    if (-not (Test-Path $probe)) { throw "oneTBB binaries are missing in the main checkout: $probe" }
    Write-Output "DEPS OK: $destinationRoot"
}

switch ($Command) {
    'new' {
        $path = Get-WorktreePath $Branch
        if (Test-Path $path) { throw "Already exists: $path" }
        New-Item -ItemType Directory -Force -Path $worktreeRoot | Out-Null
        git -C $mainRoot show-ref --verify --quiet "refs/heads/$Branch"
        if ($LASTEXITCODE -eq 0) {
            git -C $mainRoot worktree add $path $Branch
        }
        else {
            if ($Base -like 'origin/*') { git -C $mainRoot fetch origin ($Base -replace '^origin/', '') }
            git -C $mainRoot worktree add -b $Branch $path $Base
        }
        if ($LASTEXITCODE -ne 0) { throw "git worktree add failed: $Branch" }
        Copy-VendoredBinaries $path
        Write-Output "WORKTREE: $path [$Branch]"
    }
    'deps' {
        Copy-VendoredBinaries (git -C (Get-Location) rev-parse --show-toplevel).Trim()
    }
    'list' {
        git -C $mainRoot worktree list
    }
    'remove' {
        $path = Get-WorktreePath $Branch
        $arguments = @('worktree', 'remove', $path)
        if ($Force) { $arguments += '--force' }
        git -C $mainRoot @arguments
        if ($LASTEXITCODE -ne 0) { throw "git worktree remove failed: $path (uncommitted changes? use -Force)" }
        git -C $mainRoot worktree prune
        Write-Output "REMOVED: $path (branch '$Branch' kept; delete it after the PR is merged)"
    }
}
