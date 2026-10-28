# AGENTS.md

CPU 소프트웨어 렌더러(C++23/26, 자체 Job System `sr::jobs`, SSE/AVX). 목표는 **기본 데모 장면을 1920×1080에서 최소 100fps**로 그리는 것이다(느린 프레임 포함).
이 파일은 이 저장소에서 일하는 **모든 AI 도구(Claude Code, Gemini CLI 등)의 공통 규칙**이다. `CLAUDE.md`와 `GEMINI.md`는 이 파일을 불러오는 입구일 뿐이니
규칙은 **여기에만** 적는다. 사람용 설명은 README(3개 언어), 상세 계약은 MAINTENANCE.md, 파일별 역할은 CODEMAP.md에 있다.

## 모든 코드 작업의 순서

1. 코드 변경 (한 번에 한 가지)
2. 테스트 통과 확인: `./tests/RunTests.ps1 -Quick` (골든 이미지 포함)
3. 벤치(`srcli bench`, 1920x1080, PC의 MSVC Release x64)와 프로파일링(VTune **와** VS Profiler 둘 다)으로 점유율 높은 코드를 찾고 변경 전 기준값을 기록
4. 생성된 어셈블리를 보면서 최적화 (`/FAs`, `dumpbin /disasm`)
5. 다시 테스트와 벤치: **픽셀 해시가 같고 ms가 줄었을 때만** 채택
6. 전후 수치(모드별 변화율·95% 구간, pixel_hash 동일, VTune·VS 상위 핫스팟과 점유율 %, 어셈블리 전/후 발췌)를 커밋 메시지에 적고 커밋, 푸시 (CI가 다시 확인)

코드 줄 수가 아니라 측정된 fps/ms로 판단한다. 어셈블리 원칙은 프레임 경로와 로드 시점 코드(디코더, inflate, 파서)에 적용하고 `tests/`, `tools/`는 제외한다.

## 사용자와 일하는 방식

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

## 여러 AI가 번갈아·동시에 작업할 때

Claude Code와 Gemini CLI가 날에 따라 번갈아, 또는 같은 날 동시에 이 저장소에서 일한다. 서로의 대화·메모리는 보이지 않으므로
**공유할 것은 전부 git과 PR에 남긴다.**

- **규칙의 원본은 이 파일 하나다.** 새로 알게 된 규칙·함정·버린 시도·사용자 결정은 자기 도구의 메모리(Claude 프로젝트 메모리, Gemini `/memory add`)에만 두지 말고
  이 파일의 해당 절에 적어 PR로 올린다. 문서만 바꾸는 작은 PR이면 충분하다(`docs/<주제>` 브랜치). 도구 전용 메모만 `CLAUDE.md`/`GEMINI.md`에 둔다.
- **작업 하나 = 브랜치 하나 = worktree 하나 = draft PR 하나.** 시작 전에 다른 AI가 같은 곳을 만지는지 본다:
  ```powershell
  git fetch origin; gh pr list --repo namsy8471/SoftwareRendererProject --state open   # 열린 PR과 브랜치
  ./tools/Worktree.ps1 list                                                         # PC에 열린 worktree
  ```
  같은 파일·같은 함수를 고치는 열린 PR이 있으면 그 작업이 끝날 때까지 기다리거나 다른 일을 고른다.
- **하던 일의 상태는 draft PR 설명에 남긴다.** 끝나지 않은 작업은 일찍 push해 draft PR을 열고, 설명 맨 아래 `## 진행 상황` 절에
  한 일 / 측정값(작업 폴더 경로 포함) / 다음 단계 / 막힌 점을 적어 둔다. 다음 날 다른 AI는 이 절을 읽고 이어받는다.
  이어받을 때는 그 브랜치에 새 커밋을 쌓고, **다른 AI가 만든 브랜치를 force-push로 고쳐 쓰지 않는다**(리베이스가 필요하면 PR에 적고 진행).
- **PC 벤치·프로파일은 둘 다 Job Runner 큐로만 돌린다**(아래 "PC 환경 → PC 측정은 Job Runner 큐로"). 직접 실행하면 다른 AI의 측정과 겹쳐 둘 다 무효가 된다.
  큐 폴더 이름의 `<작업명>` 앞에 도구 이름을 붙인다(예: `20261028-131500-gemini-tile-index`, `...-claude-vec3-copy`).
- **누가 했는지 남긴다.** 커밋 메시지 끝에 도구 trailer를 붙인다: Claude는 `Co-Authored-By: Claude ...`(Claude Code가 붙이는 줄), Gemini는 `Generated-By: Gemini CLI`.
  PR 설명 첫 줄에도 작성 도구를 적는다.
- **main이 바뀌면 열린 PR을 확인한다.** 다른 AI의 PR이 머지된 뒤에는 자기 PR이 충돌하지 않는지 보고, 필요하면 리베이스한 뒤 CI 녹색을 다시 확인한다.


## 성능 기준 (100fps 판정)

- 측정 도구: `srcli bench` (창 없음, 고정 dt 1/60, 60프레임 warmup 뒤 600프레임).
- 역할 구분: `srcli bench`는 합격/불합격을 가르는 **판정용 숫자**, VTune·VS CPU Usage(`tests/CaptureCpuProfiles.ps1`)는 **어디가 느린지 찾는 진단용**이다.
  프로파일러는 측정 자체에 오버헤드가 있어 그 수치(ms)로 판정하지 않는다. 사용 순서는 아래 "최적화 작업 흐름" 2단계.
- **프로파일링은 필수다(사용자 결정, 2026-10-22).** 모든 최적화는 PC에서 **VTune과 VS Profiler(VS CPU Usage) 둘 다**로
  변경 전과 후를 프로파일링해 성능을 확인한다. 핫스팟 상위 함수와 점유율(%)의 전후를 커밋 메시지·PR 설명에 적는다.
  callgrind(Ir), 클라우드 A/B, Linux/GCC 수치는 보조 자료일 뿐이며 이 두 프로파일러를 대신하지 않는다.
- 한 프레임 = `FrameRunner::Update`(Scene 갱신 + 가시성 수집) + `FrameRunner::Render`(`RenderTarget::Clear` + `Renderer::RenderScene`). GDI 표시는 제외.
- **상대 비교**(이 변경이 이전 코드보다 빠른가)는 변경의 종류에 따라 측정 장소를 고른다.
  - 스레드 수에 덜 민감한 변경 → **클라우드 A/B 벤치**(`.github/workflows/ab-bench.yml`).
    예: 한 스레드 안의 계산(SIMD, 분기 제거, 인라인, 메모리 배치, 수학 함수, 클리핑·셰이딩 식).
    같은 VM에서 base·head를 번갈아 재서 변화율과 95% 구간을 낸다. PR에 `ab-bench` 라벨을 붙이면 돌고
    (`gh pr edit <번호> --add-label ab-bench`), PR 없이도 `gh workflow run ab-bench.yml --ref <브랜치>`로 돌린다. PC를 쓰지 않는다. 읽는 법은 `tools/ci/README.md`.
  - 스레드 수에 민감한 변경 → **PC A/B**(`./tests/Bench.ps1 -Runs 3`로 변경 전후, PC 순서를 받아서).
    예: 타일 크기, `sr::jobs` 분할·grain size, 작업 분배, 스레드 간 동기화·공유 데이터, 스레드별 버퍼, 병렬 단계 순서.
    클라우드는 4 vCPU라 24코어 PC와 결과가 다를 수 있다. 애매하면 클라우드로 먼저 보고 PC에서 확인한다.
- **절대 기준**(아래 p95 <= 9ms)은 PC 벤치로만 판정한다. 변경마다 재지 않고 마일스톤에서만 잰다
  (main 병합 뒤 자동 실행, 기준 근처의 최적화를 마무리할 때).
- 절대 기준 판정: Windows PC의 MSVC **Release x64** 빌드에서 `fill`, `wire`, `normal` 세 모드 모두 **`p95_ms <= 9`**.
  10ms(100fps)에서 GDI 표시 비용 여유 1ms를 뺀 값이다. `mean_ms`는 참고용이다.
- 잡음: PC는 모드마다 3회 실행해 p95의 중앙값으로 판정한다(`./tests/Bench.ps1 -Runs 3`). 개선은 같은 조건에서 **평균 1% 이상** 줄었을 때 인정한다 (초기 2% 기준에서 현재 1080p fill 프레임 시간이 약 3.0ms 수준으로 단축됨에 따라 2026-10-28 완화).
  클라우드 A/B는 95% 구간이 0을 넘지 않고(유의) **평균 1% 이상** 줄었을 때만 개선으로 본다(표의 ✅).
- 가끔 표시 포함 경로(`tests\bin\x64\Release\RendererTests.exe . --benchmark 600 1920 1080 fill none`)로 교차 확인한다.
- 클라우드의 절대 ms(Linux/GCC CI, A/B 표의 ms)는 PC와 다르므로 절대 기준 판정에 쓰지 않는다. 클라우드는 비율만 본다.
- 클라우드 A/B의 한계: 약 3%보다 작은 차이는 "구분 불가"로 나오기 쉽다. 이때는 `rounds`를 늘려 다시 재거나 PC에서 확인한다.
- 성능 변경은 화면을 바꾸면 안 된다. 골든 테스트 통과 + 같은 설정의 `pixel_hash`가 변경 전후 같아야 한다
  (의도적인 화면 변경은 별도 작업으로 분리하고 골든을 갱신한다).

### 채택 판정 보충 (2026-10-28 사용자 결정, 위 내용과 다르면 이것이 우선)

- 판정: **유의(구간이 0을 넘지 않음) AND 평균 1% 이상**. (2026-10-28 사용자 결정: 현재 1080p fill 프레임 시간이 약 3.0ms 수준까지 최적화되어 2%(=0.06ms) 개선 기준은 노이즈 플로어 대비 과도하게 가혹하므로, 평균 1% 이상 단축 시 PR 및 채택 진행). 잡음 수준은 약 ±1.9%(A/A 측정, 라운드 수 증가로 통계적 유의성 확보).
- **fill 위주로 판단**한다. wire·normal은 "1%보다 더 느려지지 않았는가"만 적은 라운드로 확인한다.
- 경계선 결과는 라운드를 늘려 다시 잰다(8라운드 조기 종료가 +3.95%를 거짓으로 낸 적이 있다). `-MinRounds 20 -MaxRounds 30~40`.
- PC A/B가 이미 통과했으면 push/PR 전에 PC 테스트를 **다시 돌리지 않는다**. 바로 push + draft PR.
- 한 단계의 CPU 시간 절감은 24스레드에서 프레임 시간의 1~2%로 줄어드는 일이 흔하다. VTune에서 함수가 -18%여도 wall은 안 바뀔 수 있다. 판정은 wall(A/B)로 한다.
- 성능 변경은 화면을 바꾸면 안 된다(pixel_hash 동일). 의도한 화면 변경은 별도 작업 + 골든 갱신.

## 명령

Windows (저장소 루트, PowerShell 7):

```powershell
./tests/RunTests.ps1 -Quick                     # x64 Release 빌드 + 전체 테스트 (실패와 SUMMARY만 출력)
./tests/RunTests.ps1 -Quick -Filter renderer    # 이름 일부로 테스트 선택 (--list로 이름 확인)
./tests/RunTests.ps1 -Rebuild                   # Debug/Release × Win32/x64 전부. 마무리 확인용
./tests/Bench.ps1 -Label before-x               # fill/wire/normal 측정 → tests/out/bench/*.jsonl
tools\bin\x64\Release\srcli.exe bench --mode wire --json
tools\bin\x64\Release\srcli.exe render --mode normal --out tests/out/normal.png
```

Linux / 클라우드 (CMake + GCC, 외부 라이브러리 없음):

```bash
tools/dev.sh test [필터]       # out/release에 빌드 + RendererTests
tools/dev.sh bench [모드...]   # srcli bench --json
tools/dev.sh render wire out.png
```

`srcli` 옵션: `--size WxH --frames N --warmup N --mode fill|wire|normal|aabb|fxaa --opt none|sort|hiz|both
--hidden-wire --dda --stats --threads N --load-every N --target-fps F --require-target --json --out file.png`.
`--load-every N`은 측정 중 N프레임마다 teapot.obj를 백그라운드로 로드한다(결과는 버려 픽셀 동일). 로딩이 p95를 흔드는지 볼 때 쓴다.
`--scene`은 데모 대신 glTF 장면을 가장 긴 변 4로 맞춰 그리고 `--clip` 애니메이션을 재생한다(자동 회전 끔). JSON에 `skinned_vertices`, `joints`,
`anim_ms`(포즈 + 계층 transform + 스키닝 한 번의 평균, 측정 프레임 뒤 따로 잼)가 붙는다. 데모 장면 판정 수치와 섞지 않는다.
`--stats`는 패스별 시간(`prepare/bin/raster/line/post_ms`)과 카운터를 JSON에 넣지만 측정을 느리게 하므로 최종 수치에는 쓰지 않는다.

## 어셈블리 수준 최적화 원칙

**모든 코드는 실제 코드보다 어셈블리 단에서 최적화가 이루어진 코드여야 한다.**

적용 방법:
- 바꾼 함수는 생성된 어셈블리를 직접 본다. MSVC는 `/FAs`(소스가 섞인 .asm)나 `dumpbin /disasm`,
  GCC는 `-S -fverbose-asm`이나 `objdump -d -C --no-show-raw-insn`을 쓴다.
- 소스 수준의 추측("이렇게 쓰면 빠르겠지")이 아니라 어셈블리에 보이는 것을 근거로 고친다:
  인라인 여부, 분기와 cmov, SIMD 폭과 명령, 불필요한 load/store와 레지스터 spill, 호출 오버헤드, 루프 벡터화.
- 커밋 메시지나 PR 설명에 바뀐 코드의 어셈블리 전/후 발췌(핵심 명령과 명령 수 변화)를 적는다.
  채택 여부는 아래 6단계처럼 측정 수치(A/B 판정, pixel_hash 동일)로 정한다.

적용 범위 (사용자 결정, 2026-10-21):
- 적용: 프레임마다 도는 코드. 래스터화, 정점 변환, 타일 binning/scatter, 선 그리기, 후처리(FXAA),
  애니메이션 샘플링·스키닝, 장면 갱신·가시성 수집 등 프레임 경로 전부.
- 적용: 로드 시점 코드. 이미지 디코더(PNG/JPEG/BMP/TGA/DDS, inflate), glTF·JSON·OBJ/MTL 파서와 모델 후처리.
- 제외: `tests/`, `tools/`처럼 배포되지 않는 코드.

## 최적화 작업 흐름

한 번에 한 가지 변경만 하고, 아래 순서를 지킨다.

1. 기준 측정: 클라우드 A/B가 base를 직접 빌드해 재므로 따로 기록하지 않아도 된다. PC로 잴 때만 `./tests/Bench.ps1 -Runs 3 -Label before-<주제>`.
2. 프로파일(필수, PC): `tests/CaptureCpuProfiles.ps1 -OutputDirectory tests/bin/profiles/before-<주제> -Analyze`로 VS CPU Usage와 VTune을
   **둘 다** 수집해 핫스팟 함수와 점유율을 기록하고, 해당 함수의 어셈블리(`/FAs` 또는 디스어셈블리)를 본다. 한쪽만 쓰지 않는다.
3. 수정: 핫스팟만 고친다. 위 "어셈블리 수준 최적화 원칙"에 따라 어셈블리(인라인 여부, 분기, SIMD 사용, 메모리 접근)를 보고 고친다.
4. 테스트: `./tests/RunTests.ps1 -Quick` 전부 통과(골든 포함).
5. 재측정: 브랜치를 푸시하고 `gh workflow run ab-bench.yml --ref <브랜치>`(PR이 있으면 `ab-bench` 라벨)로 클라우드 A/B 표를 받는다.
   PC로 확인할 때는 `./tests/Bench.ps1 -Runs 3 -Label after-<주제>`.
   프로파일도 다시 잡는다(필수): `-OutputDirectory tests/bin/profiles/after-<주제>`로 VS CPU Usage와 VTune 모두. 고친 함수의 점유율이 실제로 줄었는지 확인한다.
6. 채택 조건: A/B 표에서 `pixel_hash`가 같고 판정이 ✅ 개선일 때만 남긴다. ❌ 느려짐이면 되돌린다.
   ➖ 구분 불가면 `-f rounds=60`으로 다시 재고, 그래도 구분 불가면 PC에서 잰다. 스레드에 민감한 변경은 처음부터 PC A/B로 판단한다.
7. 커밋·푸시: 마무리 전 `./tests/RunTests.ps1 -Rebuild`(4개 구성) 통과 후, 커밋 메시지에 모드별 A/B 변화율(95% 구간)과
   pixel_hash 동일 여부, VTune·VS CPU Usage의 전후 핫스팟(상위 함수와 점유율 %)을 적고 푸시한다. PC에서 쟀다면 PC 전후 mean/p95(ms)도 적는다.

## 화면 확인

- `srcli render ... --out x.png`로 저장한 PNG를 직접 열어 본다.
- 골든: `tests/golden/{fill,wire,wire-hidden,normal,aabb,fxaa}.png` (320×180, warmup 30프레임).
  `anim.png`, `anim-normal.png`는 데모 대신 `tests/fixtures/gltf/skinned-tube.glb`(0.5초, Bend 45도)를 그린다.
  실패하면 `tests/out/golden/<이름>.actual.png`, `<이름>.diff.png`(다른 픽셀=빨강)가 생긴다.
- 허용 오차(채널 4, 픽셀 0.2%)는 `tests/GoldenTests.cpp` 상단에 있다. 의도한 변경일 때만
  `-UpdateGolden`(Windows) 또는 `tools/dev.sh golden`으로 갱신하고 PNG를 같이 커밋한다.

## 코드 지도

폴더 단위 요약이다. 파일 하나하나의 역할과 핵심 타입은 [CODEMAP.md](CODEMAP.md)에서 찾는다(파일을 열기 전에 먼저 본다).

| 위치 | 내용 |
| --- | --- |
| `src/Renderer/Renderer.cpp` | 프레임 순서: prepareLights → prepareFrame → binCommands → renderTiles → renderLines → applyPostProcessing |
| `src/Renderer/RendererFrontend.cpp` | 정점 변환·캐시, 클리핑 호출, 16×16 타일 참조 |
| `src/Renderer/Rasterizer.cpp`, `RendererShading.cpp` | 삼각형 coverage·깊이·Hi-Z·선, Phong |
| `src/Renderer/RendererClipping.cpp` | 동차 클리핑 |
| `src/Core/FrameRunner.*` | 플랫폼 무관 프레임 순서(프레임 시작 훅 → Scene::Update → 수집 → Clear → RenderScene). Win32App·srcli·골든이 공유 |
| `src/Core/SceneRenderCollector.*` | 장면 → RenderQueue 변환(메시별 병렬 batch, 옥트리 질의, 법선/AABB 디버그 선). DebugFlags는 여기서만 읽는다 |
| `src/Scene/Scene.*`, `GameObject.cpp`, `src/Graphics/Octree.h` | 장면 데이터(루트 물체·광원·카메라), transform 계층, 보이는 옥트리 노드 질의. 렌더 명령을 모른다 |
| `src/Core/DemoScene.*` | 벤치·골든 전용 고정 장면(`sr::demo::LoadScene` 모델 표, `FitCameraToScene`) |
| `src/Core/HeadlessRunner.*`, `tools/srcli/` | 창 없는 실행·측정·PNG |
| `src/App/Win32App.*`, `src/Platform/` | Win32 창·메뉴·입력·GDI 표시 (Windows 전용, CMake 빌드에서 빠짐) |
| `src/IO/` | 자체 JSON(`Json.*`), 백그라운드 로드 큐(`BackgroundLoader.*`: 작업 스레드 파싱 → 프레임 시작 훅에서 적용), 파일 읽기·원자적 쓰기 |
| `src/IO/Gltf.*`, `src/Graphics/GltfLoader.*` | 자체 glTF 2.0 로더. `Gltf`는 .gltf/.glb 문서 파싱·범위 검증·accessor 읽기, `GltfLoader`는 node 계층 → GameObject 트리, primitive → Mesh, baseColor → Phong 재질, skin/animation 데이터. 표본은 `tests/fixtures/gltf/generate.py` |
| `src/Project/` | 프로젝트 폴더(`.srproj` + `Scenes/*.srscene` + `Assets/` + `Cache/`) 형식·읽기·쓰기(`ProjectFiles.*`), 열린 프로젝트의 비동기 열기·저장(`ProjectSession.*`, Win32 파일 메뉴는 `src/App/Win32AppProject.cpp`) |
| `src/Animation/` | `Animator`(glTF 클립 재생: 재생/정지·속도·반복·클립 전환, 키프레임 → node 로컬 TRS), `AnimationSampling`(STEP/LINEAR/CUBICSPLINE, 회전 slerp), `Skinning`(CPU 선형 블렌드 스키닝, `SkinningAVX.cpp`만 AVX). 순서: `Scene::Update`가 포즈 → transform → 스킨. 스킨 메시는 월드 공간 변형 버퍼를 world=단위 행렬로 그리고, Octree 대신 변형 정점 AABB로 컬링한다. 쿼터니언은 `src/Math/Quaternion.h` |
| `src/Math/` | SRMath(SSE), `SIMD_AVX.cpp`만 AVX로 컴파일. `Quaternion.h`(곱, slerp/nlerp, 행렬·축각 변환) |
| `src/Graphics/Codecs/` | 자체 이미지 코덱(PNG/JPEG/BMP/TGA/DDS 디코더, zlib inflate, 골든용 PNG 인코더). 메모리 버퍼 → RGBA8. fixture와 기준 해시는 `tests/fixtures/images`(`tools/make_image_fixtures.py`) |
| `tests/` | `TestMain.cpp` 실행기, 영역별 `*Tests.cpp`, `golden/`, `fixtures/` |

## 규칙

- 공개 멤버 함수 PascalCase, 내부 함수·지역 변수 camelCase, 멤버 `m_camelCase`. `SRMath`의 snake_case는 유지.
- 주석은 한국어. 좌표계·수명·스레드 계약처럼 이름만으로 모호한 것만 적는다.
- MSVC `/W4 /WX`, GCC `-Wall -Wextra -Werror`. 경고 0을 유지한다. `src/ThirdParty`는 수정하지 않는다.
- 컴파일러 전용 문법(`__forceinline`, intrinsic 탐지)은 `src/Platform/Compiler.h`나 `SIMD.cpp`처럼 한곳에 감싼다.
- 새 `.cpp/.h`는 `build/RuntimeSources.props`(앱·테스트·srcli 공통)에 추가한다. CMake는 `src/` 아래 `.cpp`를 자동으로 모은다.
  `CODEMAP.md`의 해당 폴더 표에도 한 줄 추가한다(파일 이동·삭제도 같이 반영). 빠지면 CI `Check CODEMAP.md`가 실패한다.
  새 테스트 파일은 `RendererTests.vcxproj`에도 추가하고, 테스트는 그 파일의 `Register...Tests`에 등록한다.
- `src/Modules/sr.math.ixx`(MSBuild)와 `src/Modules/sr.math.h`(CMake, `SR_NO_MODULES`)는 같은 concept을 담는다. 함께 고친다.
- 장면 구성을 바꾸면(`DemoScene.cpp`의 표) 벤치마크 기준과 골든이 바뀐다. 성능 작업과 섞지 않는다.
- 픽셀 소유권·같은 깊이에서 작은 primitive ID 우선·명령 순서 보존 같은 결정성 계약은 MAINTENANCE.md "변경할 때 지킬 계약"을 따른다.

## 문서

- `PERFORMANCE.md`(약 90KB)와 `OPTIMIZATION_REPORT.md`는 과거 프로파일링 기록이다. 필요한 절만 찾아 읽는다.
- 새 성능 결과는 `tests/Bench.ps1`의 jsonl을 근거로, 변경 전후 mean/p95/pixel_hash를 짧게 남긴다.
- 프로파일러 스크립트: `tests/CaptureCpuProfiles.ps1`(VS CPU Usage, VTune). 산출물은 `tests/bin/profiles`(git 제외).

## 작업 공간 (git worktree)

여러 작업(스레드)이 한 PC에서 동시에 진행되므로 **작업마다 worktree 하나, 브랜치 하나**를 쓴다.
메인 체크아웃(`...\GitHub\SoftwareRendererProject`)에서 브랜치를 바꾸거나 남의 미커밋 변경을 건드리지 않는다.

```powershell
./tools/Worktree.ps1 new perf/normal-shading        # origin/main에서 브랜치 생성 → ..\SR-worktrees\perf-normal-shading
./tools/Worktree.ps1 new fix/x -Base <브랜치>       # main이 아닌 브랜치에서 시작
./tools/Worktree.ps1 new <기존 브랜치>              # 이미 있는 브랜치를 연다
./tools/Worktree.ps1 list
./tools/Worktree.ps1 remove perf/normal-shading     # PR 병합 뒤 정리. 미커밋 변경이 있으면 실패(-Force)
```

- 경로: `..\SR-worktrees\<브랜치 이름의 / 를 - 로 바꾼 것>`. 모든 스크립트(`RunTests.ps1`, `Bench.ps1`, srcli 자산 탐색)는
  worktree 기준 상대 경로로 동작하고, 빌드 산출물(`tests/bin`, `tools/bin`, `x64/`)도 worktree마다 따로 생긴다.
- 외부 런타임 DLL이 없다(병렬 작업은 `src/Utils/Jobs.*`의 자체 스레드 풀). `.lib/.dll/.pdb`는 `.gitignore` 대상이다.
- 브랜치 이름: `perf/<주제>`(최적화), `fix/<주제>`, `ci/<주제>`, `refactor/<주제>`, `docs/<주제>`, `tooling/<주제>`.
- 흐름: worktree에서 커밋 → `git push -u origin <브랜치>` → main 대상 PR(초안) → CI 녹색 → 병합 → `remove`.
- 벤치·프로파일은 PC 전체 CPU를 쓰므로, 다른 worktree에서 빌드가 도는 동안 잰 수치는 판정에 쓰지 않는다.

## 환경 메모

- 앱·MSBuild 빌드는 Windows + Visual Studio(v145 도구체인)가 필요하다. 클라우드 세션은 CMake/GCC로 코어·srcli·테스트만 빌드할 수 있다.
- Release의 LTCG(`/GL`)는 MSVC 내부 오류 때문에 꺼져 있다(BuildSettings.props 주석). 다시 켤 때는 4개 구성 테스트와 벤치를 함께 확인한다.

## CI와 PC 러너

- 빌드·테스트·`/analyze`·ASan·A/B 벤치는 GitHub 클라우드(Linux, Windows)에서 돈다. PR에서는 Linux 2개 + MSVC Release·`/analyze`만
  돌고(약 10분), MSVC ASan은 main push·수동, A/B는 `ab-bench` 라벨·수동 때만 돈다. `.md`만 바꾼 PR은 Windows를 건너뛴다. PC(self-hosted runner, 라벨 `renderer-pc`)는
  `windows.yml`의 `bench` job(절대 기준)만 맡고, main push와 수동 실행 때만 돈다(PR마다 돌지 않는다). 구성 설명과 등록 방법은 `tools/ci/README.md`.
- PC 벤치 job이 필요하거나 대기 중인데 러너가 꺼져 있으면, 사용자에게 묻지 말고 러너를 켠다.
  끄는 것은 사용자가 요청할 때만 한다.
  ```powershell
  gh api repos/namsy8471/SoftwareRendererProject/actions/runners --jq '.runners[] | .name+" "+.status'  # online/offline
  Get-Process Runner.Listener -ErrorAction SilentlyContinue                                              # 실행 중이면 프로세스가 보인다
  Start-Process C:\actions-runner\run.cmd -WorkingDirectory C:\actions-runner                            # 켜기(별도 창)
  Stop-Process -Name Runner.Listener                                                                     # 끄기(요청 시에만)
  ```
- 다른 작업이 PC에서 A/B 벤치를 재는 동안에는 러너 벤치와 겹치지 않게 한다(push가 벤치 job을 띄운다).

## PC 환경

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

## MSVC 빌드 함정과 CI 보충

- PR CI: Linux(GCC) 2개 + MSVC Release·`/analyze`. Windows 빌드는 `/analyze` + `/WX`라서 **GCC가 통과해도 MSVC에서 실패**할 수 있다:
  - **C6011** (null 역참조 가능): 비트마스크로만 보호된 배열에서 꺼낸 포인터. 명시적 null 검사를 넣는다.
  - **C6262** (스택 16KB 초과): 큰 배열 멤버(예: 64개짜리 `std::array<RenderStats>`)가 Renderer를 키우면, 테스트가 Renderer를 스택에 올릴 때 실패. 큰 배열은 힙에 둔다.
  - **C4324** (`alignas` 패딩 경고 → /WX로 실패): 클래스 멤버에 `alignas(64)`를 쓰지 않는다(필요하면 명시적 패딩).
- MSVC 어셈블리 메모: `[&]` 람다가 인라인되지 않을 수 있다(asm에서 call 확인). `std::countr_zero`는 호출마다 `__isa_available` 분기를 넣는다(`_BitScanForward64` 사용).
  강제 인라인이 오히려 함수를 키워 느려진 적이 있다(binPrepared: processCommand 841→995 명령, fill +2.1%). `SRMath::vec3` 값 복사는 `_mm_set_ps`로 다시 조립된다(`.m128` 직접 사용).
- A/B는 PR에 `ab-bench` 라벨이나 수동 실행 때만, MSVC ASan은 main push·수동 때만 돈다. `.md`만 바꾼 PR은 Windows를 건너뛴다.
- 소스 파일을 추가·이동·삭제하면 `CODEMAP.md`도 고친다(CI `Check CODEMAP.md`).

## 브랜치·PR·머지

- 브랜치 이름은 "작업 공간" 절의 규칙(`perf/`, `fix/`, `docs/` …). PR은 **draft**로 연다.
- PR 설명: **국문 먼저, 그 아래 영문**. 개선된 점(성능 수치, 해결된 문제)이 있으면 **맨 앞에 전후 비교 표**로.
- PR **assignee와 reviewer는 항상 `namsy8471`**. `miziyo`는 지정하지 않는다(작성자가 namsy8471이면 리뷰 요청은 실패할 수 있다. assignee는 꼭 넣는다).
  `gh pr create --draft --base main --head <브랜치> --title ... --body-file pr-body.md --assignee namsy8471`
- 머지: **CI가 모두 녹색이면 머지해도 된다**(사용자 상시 승인, 2026-10-21). 저장소는 squash가 아니라 **merge commit**을 쓴다.
  리베이스한 커밋의 CI가 녹색인 것을 본 뒤에만 머지한다.
- 병렬 PR은 하나가 머지될 때마다 서로 충돌할 수 있다. 머지 뒤 남은 PR을 확인하고 리베이스한다.
- 머지된 원격 브랜치 정리는 사용자가 돌리는 블록으로 한다. "PC 환경"의 "지우면 안 되는 것"은 제외.

## 시도했다가 버린 방법 (다시 하지 않는다)

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

## 남은 일 (시작 안 함)

1. 프로젝트가 열린 상태에서 glTF를 끌어다 놓으면 Assets로 복사되지 않는다.
2. 씬 저장이 애니메이션 회전을 저장하지 않는다.
3. 앱을 손으로 써 본 사람이 없다: .glb + P / `[` `]` 키, teapot 텍스처.
4. `SRMath::vec3` 값 복사가 `_mm_set_ps`로 다시 조립된다.
5. `DrawTriangleImpl` 픽셀마다 인덱스 계산(sub 2개 + imul)을 행 단위로 올릴 수 있다.
6. 이 파일의 "성능 기준" 본문과 `SRJob.ps1`에 fill 위주 판정 규칙("채택 판정 보충") 반영.
7. #32 효과를 실제 전면 앱에서 확인.
다음 로드맵: 6 에디터 UX, 7 렌더 품질.
