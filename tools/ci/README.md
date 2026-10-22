# CI 구성

| workflow | 어디서 | 하는 일 |
| --- | --- | --- |
| `.github/workflows/ci.yml` `linux` | GitHub 공용 Ubuntu | `CODEMAP.md` 누락 검사(`check_codemap.py`), CMake+GCC 빌드, 단위·골든 테스트, 참고용 벤치 |
| `.github/workflows/ci.yml` `linux-sanitize` | GitHub 공용 Ubuntu | ASan+UBSan 빌드로 같은 테스트 + srcli 모드별 짧은 실행 |
| `.github/workflows/windows.yml` `cloud` (2개 동시) | GitHub 공용 Windows (windows-2025, VS 2026) | Release x64 `RunTests.ps1 -Quick`, `/analyze` 빌드(`/p:SrAnalyze=true`)+테스트. `.md`만 바꾼 PR·push는 건너뛴다 |
| `.github/workflows/windows.yml` `asan` | GitHub 공용 Windows | main push·수동 실행 때만. Debug x64 ASan 빌드+테스트(약 15분). PR에서는 `linux-sanitize`가 같은 코드를 검사한다 |
| `.github/workflows/windows.yml` `bench` | 사용자 PC (self-hosted) | main push·수동 실행 때만. `srcli`만 Release x64로 빌드, 1080p `Bench.ps1 -Runs 3`, 절대 기준(p95 <= 9ms)과 main 대비 비교표 |
| `.github/workflows/ab-bench.yml` (모드마다 1개 동시) | GitHub 공용 Windows (windows-2025) | `ab-bench` 라벨이 붙은 PR·수동 실행 때만. 같은 VM에서 base·head `srcli`를 빌드해 1080p로 번갈아 측정, 변화율·95% 구간·pixel_hash 비교 → 요약 + PR 댓글 |

PC는 절대 기준 벤치만 맡는다. 100fps 판정(p95 <= 9ms)은 이 PC 수치라서 클라우드로 옮길 수 없고, "이전 코드보다 몇 % 빠른가"는 아래 클라우드 A/B 벤치로 잰다.
클라우드 Windows는 4코어라 이 PC(24코어)보다 빌드가 몇 배 느리지만 PC가 꺼져 있어도 돈다.

벤치는 지금 보고만 한다(절대 기준 p95 <= 9ms 미달로 실패시키지 않음). 수치가 안정되면 `windows.yml`의
`Compare-Bench.ps1`에 `-FailOnRegressionPct 5`처럼 값을 넣어 main 대비 회귀만 실패로 만든다.

## 클라우드 A/B 벤치 (`ab-bench.yml`)

변경 하나가 이전 코드보다 빠른지는 PC 없이 클라우드에서 잰다. 절대 ms는 PC와 다르지만, 같은 VM에서 두 빌드를
번갈아 재면 VM 전체의 흔들림이 양쪽에 같이 들어가서 비율(%)은 비교할 수 있다.

- 언제 도나: `src/`, `tools/srcli/`, `assets/`, `build/`, `*.vcxproj`를 바꾼 PR에 **`ab-bench` 라벨을 붙였을 때**(붙이는 순간 + 그 뒤 push마다),
  또는 수동 실행(`gh workflow run ab-bench.yml --ref <브랜치>`). 라벨: `gh pr edit <번호> --add-label ab-bench`.
  PR마다 자동으로 돌지 않는 이유: 20분 넘게 걸려 머지를 늦추는데, 채택 판단은 주로 PC A/B로 하고 클라우드는 2% 이상 차이만 가린다.
- 무엇을 비교하나: PR이면 base = 브랜치가 갈라진 지점(merge-base), head = PR 마지막 커밋. 그 브랜치가 만든 변화만 본다.
- 어떻게 재나: 모드(fill/wire/normal)마다 Windows VM 하나. VM 안에서 base와 head의 `srcli`를 MSVC Release x64로 빌드하고,
  예열 한 쌍 뒤 최대 30라운드를 base→head, head→base 순서를 번갈아 가며 잰다(`tools/ci/AB-Bench.ps1`, 1920x1080, 실행마다 200프레임).
  head는 base의 중간 파일(`tools/obj`)을 남긴 채 증분 빌드해서, 바뀐 파일만 다시 컴파일한다.
- 조기 종료: 8라운드부터 4라운드마다 결과가 이미 분명한지 본다. 모드의 mean과 p95가 둘 다
  "2% 이상 바뀜"(99% 구간이 0을 넘지 않고 중앙값이 2% 이상) 또는 "2% 이상 바뀌지 않음"(95% 구간 전체가 ±2% 안)이면 멈춘다.
  화면이 다르면 8라운드에서 멈춘다. 중간에 여러 번 들여다보면 우연히 맞는 경우가 늘어나서 "바뀜" 쪽은 최종 판정(95%)보다
  엄격한 99%를 쓴다. 판정 규칙(아래 표)은 그대로다. 끝까지 재려면 `AB-Bench.ps1 -NoEarlyStop`.
  PC 판정(600프레임 × 3회)보다 실행을 짧게, 대신 여러 번 번갈아 재는 이유는 VM 속도가 몇 분 단위로 오르내리기 때문이다.
- 같은 코드는 같은 바이너리로: base와 head를 같은 폴더에서 차례로 `/Brepro`(결정적 빌드)로 빌드한다. 빌드 경로가 달라
  같은 코드가 다른 바이너리가 되는 일을 막고, 실행 폴더(exe, dll, assets)가 바이트까지 같으면 측정 없이 "바이너리 동일"로 끝낸다.
  CI·문서만 바꾼 PR이 잡음 때문에 "느려짐"으로 찍히는 일이 여기서 원천적으로 없어진다.
- 2% 기준을 개선과 느려짐에 똑같이 쓴다. 코드가 실제로 바뀌면 함수 배치(정렬)만 달라져도 1~2%는 오르내리므로,
  그보다 작은 차이는 성능 변화로 보지 않는다(CLAUDE.md의 "2% 이상 줄었을 때만 인정"과 같은 기준).
- 어떻게 읽나: `tools/ci/AB-Report.ps1`이 라운드별 head/base 비율로 변화율과 95% 구간을 낸다. 음수가 빨라진 것이다.

| 열 | 뜻 |
| --- | --- |
| mean/p95 변화 [95% 구간] | 라운드별 head/base 비율의 중앙값(튀는 라운드에 끌려가지 않는다). 구간은 중앙값의 분포 무관 95% 구간. 구간 전체가 0보다 작으면 ↓(유의하게 빨라짐), 크면 ↑ |
| 감지 한계 | 구간 절반 폭. 이보다 작은 차이는 이번 측정으로 가려낼 수 없다. `rounds`를 늘리면 줄어든다 |
| 흔들림 CV | base p95의 라운드 간 변동계수. 크면 이웃 VM 영향이 큰 실행이니 다시 돌려 본다 |
| pixel_hash | base와 head가 같아야 한다. 다르면 화면이 바뀐 것(성능 변경으로는 불합격) |
| 판정 | ✅ 개선 / ❌ 느려짐(화면 같음 + mean 또는 p95가 유의하게 2% 이상 감소 / 증가), ⚖ 엇갈림, ➖ 차이 없음(두 구간 모두 ±2% 안), ❔ 구분 불가(rounds를 늘린다), ➖ 바이너리 동일(측정 생략), ⚠ 화면 바뀜 |

결과는 각 job 요약, 전체 요약(`A/B report` job), PR 댓글 하나(새 실행마다 고쳐 씀)에 남는다. 지금은 보고만 하며
PR을 실패시키지 않는다. 회귀를 막고 싶어지면 `report` job의 `AB-Report.ps1`에 `-FailOnRegressionPct 5`를 넣는다.

### 수동 실행 (PR 없이 브랜치 비교)

Actions 탭 → **Cloud A/B bench** → **Run workflow**에서 브랜치를 고르고 입력값을 넣는다. 또는:

```powershell
gh workflow run ab-bench.yml --ref perf/my-change                          # main에서 갈라진 지점 대비, fill/wire/normal
gh workflow run ab-bench.yml --ref perf/my-change -f modes=normal -f rounds=60
gh workflow run ab-bench.yml --ref main -f base=<커밋> -f head=<커밋> -f merge_base=false   # 임의의 두 커밋
gh run watch                                                               # 진행 보기, 끝나면 요약에 표가 나온다
```

입력: `base`(기본 main), `head`(비우면 고른 브랜치), `merge_base`(기본 true: base와 head가 갈라진 지점과 비교),
`modes`(쉼표 구분), `rounds`(최대, 기본 30), `frames`(기본 200). 걸리는 시간은 빌드(base 전체 + head 증분) + 모드당 최대 62회 실행이며
모드들은 동시에 돈다. 차이가 분명하면 8라운드(18회 실행)에서 끝난다.

### 한계 (그래서 PC 벤치가 남는 이유)

- 표준 Windows runner는 4 vCPU다. PC(24 논리 코어)와 코어 수·캐시·메모리 대역폭이 달라서, 스레드 수나 작업 분할에
  따라 효과가 달라지는 변경(타일 크기, `sr::jobs` 분할, 스레드 간 동기화)은 클라우드 결과가 PC로 그대로 옮겨지지 않는다.
  그래서 스레드 수에 민감한 변경은 PC A/B로 판단하고, 덜 민감한 변경(한 스레드 안의 SIMD·분기·메모리 배치 등)은 클라우드 A/B로 판단한다
  (구분 기준은 CLAUDE.md "성능 기준").
- 이웃 VM 때문에 흔들림이 있다. 대략 3% 아래의 차이는 기본 라운드 수로는 "구분 불가"로 나오기 쉽다. 그럴 때는 `rounds`를 늘리거나 PC에서 잰다.
- 실행마다 다른 VM(다른 CPU 모델일 수도 있다)에 배정된다. 서로 다른 실행의 절대 ms끼리는 비교하지 않는다. 비율만 본다.
- GitHub Free 공개 저장소는 표준 runner job을 동시에 약 20개까지 무료로 쓴다. 더 큰 runner(코어 많은 VM)는 유료다.

## PC를 self-hosted runner로 등록하기 (한 번만)

1. GitHub 저장소 → **Settings → Actions → Runners → New self-hosted runner** → **Windows / x64**를 고른다.
   화면에 다운로드·설정 명령과 1시간짜리 등록 토큰이 나온다. 토큰은 채팅이나 파일에 붙이지 않는다.
2. PowerShell에서 저장소 **밖**에 폴더를 만든다. runner가 저장소 작업 폴더를 건드리지 않도록 하기 위해서다.
   ```powershell
   mkdir C:\actions-runner; cd C:\actions-runner
   ```
3. 1번 화면의 `Download` 명령(Invoke-WebRequest, 압축 풀기)을 그대로 실행한다.
4. 1번 화면의 `config.cmd` 명령에 이름과 라벨을 붙여 실행한다.
   ```powershell
   ./config.cmd --url https://github.com/namsy8471/SoftwareRendererProject --token <화면의 토큰> --name DESKTOP-IHVV44E --labels renderer-pc --work _work
   ```
   - runner group, 추가 라벨 질문은 Enter(기본값).
   - "Would you like to run the runner as service?"에 `Y`. PC가 켜져 있으면 자동으로 job을 받는다.
5. Settings → Actions → Runners에 `DESKTOP-IHVV44E`가 **Idle**로 보이면 끝이다.

필요한 도구(이미 설치된 것): Visual Studio 18(v145, C++ 데스크톱), PowerShell 7(`pwsh`), Git, GitHub CLI(`gh`, main 비교용).

### 안전 설정
- 공개 저장소이므로 fork에서 온 PR은 `windows.yml`의 `if` 조건으로 PC에서 돌지 않는다.
- 추가로 Settings → Actions → General → "Fork pull request workflows from outside collaborators"를
  **Require approval for all outside collaborators**로 둔다.

### 잠시 멈추기 / 지우기
- 멈추기: `services.msc`에서 `GitHub Actions Runner (...)` 서비스를 중지(관리자 PowerShell: `Stop-Service "actions.runner.*"`).
- 지우기: `C:\actions-runner\config.cmd remove --token <Settings에서 받은 제거 토큰>`.
- 벤치 job이 도는 동안 PC에서 다른 무거운 작업(다른 작업 폴더의 빌드나 `Bench.ps1` A/B 측정 포함)을 하면
  양쪽 수치가 모두 흔들린다. 직접 측정하는 동안에는 서비스를 잠시 멈추거나, CI 벤치 결과를 다시 돌려 확인한다.
- PC에서 걸리는 시간: srcli 빌드 1~2분 + 벤치(3모드 × 3회) 수 분.
