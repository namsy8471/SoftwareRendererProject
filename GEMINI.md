# GEMINI.md

Gemini CLI용 작업 안내다. 판정 기준·명령·코드 지도·규칙의 본문은 CLAUDE.md 하나에만 두고, 아래 줄로 그대로 불러온다
(Gemini CLI의 `@파일` 가져오기). 이 파일에는 CLAUDE.md에 없는 것만 적는다: 작업 순서와 보고 방식, 채택 판정 보충 규칙,
PC 측정 방법(Job Runner), MSVC 빌드 함정, 지금까지 시도해서 버린 방법.

@./CLAUDE.md

CLAUDE.md와 이 파일이 다르면 **이 파일의 "판정 보충"이 최신**이다(2026-10-22 사용자 결정이 CLAUDE.md에 아직 반영되지 않았다).
파일별 역할은 `CODEMAP.md`를 먼저 보고 파일을 연다. 흐름·스레드 계약은 `ARCHITECTURE.md`, 바꿀 때 지킬 계약은 `MAINTENANCE.md`.

## 1. 모든 코드 작업의 순서

1. 코드 변경 (한 번에 한 가지)
2. 테스트 통과 확인: `./tests/RunTests.ps1 -Quick` (골든 이미지 포함)
3. 벤치(`srcli bench`, 1920x1080, PC의 MSVC Release x64)와 프로파일링(VTune **와** VS Profiler 둘 다)으로 점유율 높은 코드를 찾고 변경 전 기준값을 기록
4. 생성된 어셈블리를 보면서 최적화 (`/FAs`, `dumpbin /disasm`)
5. 다시 테스트와 벤치: **픽셀 해시가 같고 ms가 줄었을 때만** 채택
6. 전후 수치(모드별 변화율·95% 구간, pixel_hash 동일, VTune·VS 상위 핫스팟과 점유율 %, 어셈블리 전/후 발췌)를 커밋 메시지에 적고 커밋, 푸시 (CI가 다시 확인)

코드 줄 수가 아니라 측정된 fps/ms로 판단한다. 어셈블리 원칙은 프레임 경로와 로드 시점 코드(디코더, inflate, 파서)에 적용하고 `tests/`, `tools/`는 제외한다.

## 2. 사용자와 일하는 방식

- 사용자에게는 **한국어**로 답한다. 명령·식별자는 그대로 쓴다. 사용자는 CI/CD·git이 처음이고 "왜?"를 자주 묻는다. 용어는 쉽게 풀어 쓴다.
- **무언가를 바꿨으면 반드시 보고한다**: 어떤 파일의 어떤 부분을, 왜, 어떻게 바꿨는지 + 바뀐 코드(전/후 조각 또는 diff). 최적화면 어셈블리 변화와 전후 수치도.
- 우회책보다 **근본 원인 수정**을 원하고, **증거**(측정값, 로그, 스크립트 출력)를 원한다. 기준(임계값)을 바꿔서 통과시키지 않는다.
- PC 측정이 끝나면 **바로 숫자를 보고**한다. 다음 단계(PR 준비 등)로 먼저 넘어가지 않는다.
- push·PR 성공은 **명령 출력을 보고 나서만** 말한다(`git ls-remote origin <브랜치>`로 확인).
- 사용자가 "N 머지함"이라고 해도 번호가 틀릴 수 있다. 리베이스 전에 `git fetch origin main; git log origin/main -3`으로 실제 main을 확인한다.
- "최신화"는 "최적화/다시 쓰기"가 아니라 **기존 내용을 유지하고 새 항목·수치를 추가**하라는 뜻이다.
- PC 부하를 최소로: 빌드·테스트로 확인할 수 있는 것은 클라우드 CI(GitHub)에 맡기고, PC는 MSVC 전용 확인과 벤치에만 쓴다.
- 한 작업의 PC 단계는 **스크립트 하나**로 묶는다. 같은 걸 다시 돌리기 전에 결과 파일이 이미 있는지 먼저 본다.
- 서로 독립인 작업은 작업마다 worktree·브랜치·PR을 따로 만든다(병렬 진행 선호).

## 3. 채택 판정 보충 (CLAUDE.md보다 우선)

- 판정: **유의(구간이 0을 넘지 않음) AND 2% 이상**. 잡음 수준은 약 ±1.9%(A/A 측정).
- **fill 위주로 판단**한다. wire·normal은 "2%보다 더 느려지지 않았는가"만 적은 라운드로 확인한다.
- 경계선 결과는 라운드를 늘려 다시 잰다(8라운드 조기 종료가 +3.95%를 거짓으로 낸 적이 있다). `-MinRounds 20 -MaxRounds 30~40`.
- PC A/B가 이미 통과했으면 push/PR 전에 PC 테스트를 **다시 돌리지 않는다**. 바로 push + draft PR.
- 한 단계의 CPU 시간 절감은 24스레드에서 프레임 시간의 1~2%로 줄어드는 일이 흔하다. VTune에서 함수가 -18%여도 wall은 안 바뀔 수 있다. 판정은 wall(A/B)로 한다.
- 성능 변경은 화면을 바꾸면 안 된다(pixel_hash 동일). 의도한 화면 변경은 별도 작업 + 골든 갱신.

## 4. PC 환경

- CPU: **Intel Core Ultra 9 275HX (노트북), 하이퍼스레딩 없음**. P코어 8개(논리 0,1,10-13,22,23, 각자 L2 3MB) + E코어 16개(2-9, 14-21, 4개씩 묶여 L2 4MB 공유), L3 36MB.
  **작업 스레드를 코어에 고정(pinning)하지 않는다**: Windows가 바쁜 스레드를 P코어로 옮기지 못해 fill +7%, wire +20% 느려졌다. "SMT 형제" 최적화는 이 PC에서 의미가 없다.
- 벤치는 창 없이 돌아서 Windows가 EcoQoS를 걸 수 있다. #32의 EcoQoS 해제 효과는 실제 앱(전면 창)에서는 더 작을 수 있다(미확인).
- 저장소: `C:\Users\siyoungNam\Documents\GitHub\SoftwareRendererProject` (메인 체크아웃, 브랜치를 바꾸지 않는다).
  작업은 `./tools/Worktree.ps1 new <브랜치>` → `..\SR-worktrees\<이름>`. GitHub: `namsy8471/SoftwareRendererProject`, PC의 `gh`는 namsy8471로 로그인되어 있다.
- 도구: MSVC (VS 18, v145), VTune, pwsh 7 (`CaptureCpuProfiles.ps1`는 pwsh 7에서 실행), Windows PowerShell 5.1.

### PC 측정은 Job Runner 큐로 (벤치가 겹치지 않게)

PC에서는 다른 작업(Claude 등)도 벤치를 돌린다. 벤치 두 개가 동시에 돌면 둘 다 무효이므로 **A/B·프로파일은 직접 돌리지 말고 큐에 넣는다.**
러너가 한 번에 하나씩, 폴더 이름 순서로 실행한다. 빌드만 하거나 `RunTests.ps1 -Quick`만 돌릴 때도 큐에 `RUNNING` 작업이 있으면 끝날 때까지 기다린다.

- 큐: `C:\Users\siyoungNam\Documents\GitHub\SR-worktrees\_incoming\queue\<yyyyMMdd-HHmmss(KST)>-<작업명>\`
- 넣는 법: 폴더에 `job.ps1`(+ 번들, `job.json`)을 먼저 쓰고, **마지막에** `READY` 파일을 만든다(러너는 READY가 생겨야 본다).
- 상태: 작업 폴더의 `status.txt`, `stdout.txt`, `stderr.txt`, `exit.txt`, `DONE` / `FAILED`, 결과는 `results\`.
  러너 상태: `queue\_runner\snapshot.json`(현재 단계·대기열), `heartbeat.txt`(30초마다 갱신), `runner.log`. 일시정지: `queue\PAUSE`.
- `job.ps1` 규칙: PowerShell 5.1, 작업 하나에 스크립트 하나, 실패 시 `exit 1`(git/msbuild/gh 뒤 `$LASTEXITCODE` 확인), 한글이 있으면 UTF-8 BOM.
  `job.json`에 `"title"`, `"estimateMinutes"`, `"steps"`를 넣고, 스크립트 첫 줄 `. $env:SR_HELPERS` 뒤 각 단계 전에 `Step '<이름>'`을 부른다(이름은 steps와 같게).
- 측정 작업은 **직접 스크립트를 쓰지 말고 공용 라이브러리 `SRJob.ps1`을 쓴다** (`SR-worktrees\_incoming\_lib\SRJob.ps1`):

```powershell
. $env:SR_HELPERS
. 'C:\Users\siyoungNam\Documents\GitHub\SR-worktrees\_incoming\_lib\SRJob.ps1'
try {
    Invoke-SRStandardJob -Bundle (Join-Path $env:SR_JOB_DIR 'x.bundle') -BundleSha256 '<SHA256>' -BundleRef 'perf/x' `
        -Base '<base 전체 sha>' -Head '<head 전체 sha>'
    exit 0
} catch { Write-Host "FAILED: $_"; exit 1 }
```

  `job.json`: `{"title":"...","estimateMinutes":10,"bench":true,"timeoutMinutes":60,"steps":["MSVC 빌드","테스트","A/B 벤치","프로파일"]}`.
  옵션: `-Modes fill,wire,normal`, `-ProfileModes`, `-MinRounds/-MaxRounds`, `-Steps @('s2=<sha>')`(중간 커밋도 비교), `-Reference s2`, `-NoAB`, `-NoProfile`, `-AllowHashChange`(의도한 화면 변경만).
  커밋별 빌드는 `SR-worktrees\_cache\builds\<sha12>\`(어셈블리는 `asm\`)에, 프로파일은 `_cache\profiles\`에 캐시된다. 결과: `results\summary.txt`, `profiles.txt`, `tests-head.txt`.
  로컬 브랜치를 쓸 때는 `git bundle create x.bundle <base>..<head>`로 번들을 만들어 넣는다.
- 지우면 안 되는 것: `SR-worktrees\_build`, `_cache\*`, `_incoming\*`, `ab-*` worktree, `refs/incoming/*`, 진행 중인 `perf/*` 브랜치.

## 5. CI와 MSVC 빌드 함정

- PR CI: Linux(GCC) 2개 + MSVC Release·`/analyze`. Windows 빌드는 `/analyze` + `/WX`라서 **GCC가 통과해도 MSVC에서 실패**할 수 있다:
  - **C6011** (null 역참조 가능): 비트마스크로만 보호된 배열에서 꺼낸 포인터. 명시적 null 검사를 넣는다.
  - **C6262** (스택 16KB 초과): 큰 배열 멤버(예: 64개짜리 `std::array<RenderStats>`)가 Renderer를 키우면, 테스트가 Renderer를 스택에 올릴 때 실패. 큰 배열은 힙에 둔다.
  - **C4324** (`alignas` 패딩 경고 → /WX로 실패): 클래스 멤버에 `alignas(64)`를 쓰지 않는다(필요하면 명시적 패딩).
- MSVC 어셈블리 메모: `[&]` 람다가 인라인되지 않을 수 있다(asm에서 call 확인). `std::countr_zero`는 호출마다 `__isa_available` 분기를 넣는다(`_BitScanForward64` 사용).
  강제 인라인이 오히려 함수를 키워 느려진 적이 있다(binPrepared: processCommand 841→995 명령, fill +2.1%). `SRMath::vec3` 값 복사는 `_mm_set_ps`로 다시 조립된다(`.m128` 직접 사용).
- A/B는 PR에 `ab-bench` 라벨이나 수동 실행 때만, MSVC ASan은 main push·수동 때만 돈다. `.md`만 바꾼 PR은 Windows를 건너뛴다.
- 소스 파일을 추가·이동·삭제하면 `CODEMAP.md`도 고친다(CI `Check CODEMAP.md`).

## 6. 브랜치·PR·머지

- 브랜치 이름은 CLAUDE.md 규칙(`perf/`, `fix/`, `docs/` …). PR은 **draft**로 연다.
- PR 설명: **국문 먼저, 그 아래 영문**. 개선된 점(성능 수치, 해결된 문제)이 있으면 **맨 앞에 전후 비교 표**로.
- PR **assignee와 reviewer는 항상 `namsy8471`**. `miziyo`는 지정하지 않는다(작성자가 namsy8471이면 리뷰 요청은 실패할 수 있다. assignee는 꼭 넣는다).
  `gh pr create --draft --base main --head <브랜치> --title ... --body-file pr-body.md --assignee namsy8471`
- 머지: **CI가 모두 녹색이면 머지해도 된다**(사용자 상시 승인, 2026-10-21). 저장소는 squash가 아니라 **merge commit**을 쓴다.
  리베이스한 커밋의 CI가 녹색인 것을 본 뒤에만 머지한다.
- 병렬 PR은 하나가 머지될 때마다 서로 충돌할 수 있다. 머지 뒤 남은 PR을 확인하고 리베이스한다.
- 머지된 원격 브랜치 정리는 사용자가 돌리는 블록으로 한다. 위 "지우면 안 되는 것"은 제외.

## 7. 시도했다가 버린 방법 (다시 하지 않는다)

| 시도 | PC 결과 | 비고 |
| --- | --- | --- |
| 삼각형 binning 단계 최적화 (lazy vertex shading, SSE outcode) | fill -1.0%, wire -1.8%, normal -1.1% | 2% 미만. VTune binning CPU는 줄었지만 wall 거의 그대로 |
| 래스터 루프 coverage 비트마스크 + branchless Bresenham | fill -0.85~-1.5% | 2% 미만. branchless Bresenham은 MSVC에서 더 느림 |
| LPT(긴 타일 먼저) 타일 순서 | fill +7% | 공유 카운터로 이웃 청크가 다른 스레드에 가면 느려진다 |
| 공유 카운터로 타일 가져가기 (행 순서) | fill +20% | 같은 이유. 연속 구간 분배 + stealing 유지 |
| 작업 스레드 코어 고정(pinning) | fill +7%, wire +20%, normal +10% | 하이브리드 CPU에서 금지 |
| 와이어 밴드 행 8/4 | +6% / +9% | 선이 여러 밴드를 다시 걷는다 |
| normal 밴드 = 스레드 수 / 48, normal 선 분할 | 변화 없음~느림 | |
| 타일 버퍼 streaming NT write-back, SSE clear/copy | 느림 / 유의하지 않음 | |
| 빈 gather 범위 건너뛰기 | -0.4% | 버림 |

채택된 것(main에 있음): #19 CODEMAP, #20 octree, #21 vertex cache, #23 스레드 대기, #24 shade-once, #26 SSE 셰이딩, #27 타일 로컬 버퍼,
#28 자체 Job System `sr::jobs`(oneTBB 제거), #30 프레임 내 합류 제거, #31 비용 기반 타일 분배 `ParallelForWeighted`, #32 E코어 짧은 spin + EcoQoS 해제.
100fps 목표는 2026-10-22에 달성했다(PC, main 8df4d1b: fill p95 3.39ms, wire 4.15ms, normal 6.05ms. c152e76 대비 fill -11%, normal -15%).

## 8. 남은 일 (시작 안 함)

1. 프로젝트가 열린 상태에서 glTF를 끌어다 놓으면 Assets로 복사되지 않는다.
2. 씬 저장이 애니메이션 회전을 저장하지 않는다.
3. 앱을 손으로 써 본 사람이 없다: .glb + P / `[` `]` 키, teapot 텍스처.
4. `SRMath::vec3` 값 복사가 `_mm_set_ps`로 다시 조립된다.
5. `DrawTriangleImpl` 픽셀마다 인덱스 계산(sub 2개 + imul)을 행 단위로 올릴 수 있다.
6. CLAUDE.md와 `SRJob.ps1`에 fill 위주 판정 규칙(3절) 반영.
7. #32 효과를 실제 전면 앱에서 확인.
다음 로드맵: 6 에디터 UX, 7 렌더 품질.
