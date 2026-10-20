# CI 구성

| workflow | 어디서 | 하는 일 |
| --- | --- | --- |
| `.github/workflows/ci.yml` `linux` | GitHub 공용 Ubuntu | CMake+GCC 빌드, 단위·골든 테스트, 참고용 벤치 |
| `.github/workflows/ci.yml` `linux-sanitize` | GitHub 공용 Ubuntu | ASan+UBSan 빌드로 같은 테스트 + srcli 모드별 짧은 실행 |
| `.github/workflows/windows.yml` `build-test-bench` | 사용자 PC (self-hosted) | MSVC Release x64 빌드, `RunTests.ps1 -Quick`, 1080p `Bench.ps1 -Runs 3`, main 대비 비교표 |
| `.github/workflows/windows.yml` `analyze` | 사용자 PC | `/analyze` 정적 분석 빌드(`/p:SrAnalyze=true`) + 테스트 |
| `.github/workflows/windows.yml` `asan` | 사용자 PC | Debug x64 `/fsanitize=address` 빌드 + 테스트 |

벤치는 지금 보고만 한다(절대 기준 p95 <= 9ms 미달로 실패시키지 않음). 수치가 안정되면 `windows.yml`의
`Compare-Bench.ps1`에 `-FailOnRegressionPct 5`처럼 값을 넣어 main 대비 회귀만 실패로 만든다.

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
- 걸리는 시간(이 PC 기준): Release 빌드+테스트 수 분, `/analyze` 빌드 약 2.5분, ASan Debug 빌드+테스트 약 8분.
