# CLAUDE.md

CPU 소프트웨어 렌더러(C++23/26, oneTBB, SSE/AVX). 목표는 **기본 데모 장면을 1920×1080에서 최소 100fps**로 그리는 것이다(느린 프레임 포함).
이 파일은 AI 작업용 요약이다. 사람용 설명은 README(3개 언어), 상세 계약은 MAINTENANCE.md에 있다.

## 성능 기준 (100fps 판정)

- 측정 도구: `srcli bench` (창 없음, 고정 dt 1/60, 60프레임 warmup 뒤 600프레임).
- 역할 구분: `srcli bench`는 합격/불합격을 가르는 **판정용 숫자**, VTune·VS CPU Usage(`tests/CaptureCpuProfiles.ps1`)는 **어디가 느린지 찾는 진단용**이다.
  프로파일러는 측정 자체에 오버헤드가 있어 그 수치(ms)로 판정하지 않는다. 사용 순서는 아래 "최적화 작업 흐름" 2단계.
- 한 프레임 = `FrameRunner::Update`(Scene 갱신 + 가시성 수집) + `FrameRunner::Render`(`RenderTarget::Clear` + `Renderer::RenderScene`). GDI 표시는 제외.
- **상대 비교**(이 변경이 이전 코드보다 빠른가)는 변경의 종류에 따라 측정 장소를 고른다.
  - 스레드 수에 덜 민감한 변경 → **클라우드 A/B 벤치**(`.github/workflows/ab-bench.yml`).
    예: 한 스레드 안의 계산(SIMD, 분기 제거, 인라인, 메모리 배치, 수학 함수, 클리핑·셰이딩 식).
    같은 VM에서 base·head를 번갈아 재서 변화율과 95% 구간을 낸다. PR마다 자동으로 돌고, PR 없이도
    `gh workflow run ab-bench.yml --ref <브랜치>`로 돌린다. PC를 쓰지 않는다. 읽는 법은 `tools/ci/README.md`.
  - 스레드 수에 민감한 변경 → **PC A/B**(`./tests/Bench.ps1 -Runs 3`로 변경 전후, PC 순서를 받아서).
    예: 타일 크기, TBB 분할·grain size, 작업 분배, 스레드 간 동기화·공유 데이터, 스레드별 버퍼, 병렬 단계 순서.
    클라우드는 4 vCPU라 24코어 PC와 결과가 다를 수 있다. 애매하면 클라우드로 먼저 보고 PC에서 확인한다.
- **절대 기준**(아래 p95 <= 9ms)은 PC 벤치로만 판정한다. 변경마다 재지 않고 마일스톤에서만 잰다
  (main 병합 뒤 자동 실행, 기준 근처의 최적화를 마무리할 때).
- 절대 기준 판정: Windows PC의 MSVC **Release x64** 빌드에서 `fill`, `wire`, `normal` 세 모드 모두 **`p95_ms <= 9`**.
  10ms(100fps)에서 GDI 표시 비용 여유 1ms를 뺀 값이다. `mean_ms`는 참고용이다.
- 잡음: PC는 모드마다 3회 실행해 p95의 중앙값으로 판정한다(`./tests/Bench.ps1 -Runs 3`). 개선은 같은 조건에서 2% 이상 줄었을 때만 인정한다.
  클라우드 A/B는 95% 구간이 0을 넘지 않고(유의) 2% 이상 줄었을 때만 개선으로 본다(표의 ✅).
- 가끔 표시 포함 경로(`tests\bin\x64\Release\RendererTests.exe . --benchmark 600 1920 1080 fill none`)로 교차 확인한다.
- 클라우드의 절대 ms(Linux/GCC CI, A/B 표의 ms)는 PC와 다르므로 절대 기준 판정에 쓰지 않는다. 클라우드는 비율만 본다.
- 클라우드 A/B의 한계: 약 3%보다 작은 차이는 "구분 불가"로 나오기 쉽다. 이때는 `rounds`를 늘려 다시 재거나 PC에서 확인한다.
- 성능 변경은 화면을 바꾸면 안 된다. 골든 테스트 통과 + 같은 설정의 `pixel_hash`가 변경 전후 같아야 한다
  (의도적인 화면 변경은 별도 작업으로 분리하고 골든을 갱신한다).

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

Linux / 클라우드 (CMake + GCC, `apt install libtbb-dev` 필요):

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

## 최적화 작업 흐름

한 번에 한 가지 변경만 하고, 아래 순서를 지킨다.

1. 기준 측정: 클라우드 A/B가 base를 직접 빌드해 재므로 따로 기록하지 않아도 된다. PC로 잴 때만 `./tests/Bench.ps1 -Runs 3 -Label before-<주제>`.
2. 프로파일: `tests/CaptureCpuProfiles.ps1`(VS CPU Usage/VTune)로 핫스팟 함수를 찾고, 필요하면 해당 함수의 어셈블리(`/FAs` 또는 디스어셈블리)를 본다.
3. 수정: 핫스팟만 고친다. 어셈블리 수준(인라인 여부, 분기, SIMD 사용, 메모리 접근)까지 확인한다.
4. 테스트: `./tests/RunTests.ps1 -Quick` 전부 통과(골든 포함).
5. 재측정: 브랜치를 푸시하고 `gh workflow run ab-bench.yml --ref <브랜치>`(PR이 있으면 자동)로 클라우드 A/B 표를 받는다.
   PC로 확인할 때는 `./tests/Bench.ps1 -Runs 3 -Label after-<주제>`.
6. 채택 조건: A/B 표에서 `pixel_hash`가 같고 판정이 ✅ 개선일 때만 남긴다. ❌ 느려짐이면 되돌린다.
   ➖ 구분 불가면 `-f rounds=60`으로 다시 재고, 그래도 구분 불가면 PC에서 잰다. 스레드에 민감한 변경은 처음부터 PC A/B로 판단한다.
7. 커밋·푸시: 마무리 전 `./tests/RunTests.ps1 -Rebuild`(4개 구성) 통과 후, 커밋 메시지에 모드별 A/B 변화율(95% 구간)과
   pixel_hash 동일 여부를 적고 푸시한다. PC에서 쟀다면 PC 전후 mean/p95(ms)도 적는다.

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
./tools/Worktree.ps1 deps                           # 현재 worktree에 oneTBB 바이너리 다시 채우기
./tools/Worktree.ps1 list
./tools/Worktree.ps1 remove perf/normal-shading     # PR 병합 뒤 정리. 미커밋 변경이 있으면 실패(-Force)
```

- 경로: `..\SR-worktrees\<브랜치 이름의 / 를 - 로 바꾼 것>`. 모든 스크립트(`RunTests.ps1`, `Bench.ps1`, srcli 자산 탐색)는
  worktree 기준 상대 경로로 동작하고, 빌드 산출물(`tests/bin`, `tools/bin`, `x64/`)도 worktree마다 따로 생긴다.
- oneTBB의 링크·실행에 필요한 `tbb12[_debug].lib/.dll`은 저장소에 들어 있다(새 clone·CI에서 바로 빌드).
  그 밖의 `.lib/.dll/.pdb`는 `.gitignore` 때문에 없으므로, 필요하면 스크립트로 만들거나 `deps`를 실행한다.
- 브랜치 이름: `perf/<주제>`(최적화), `fix/<주제>`, `ci/<주제>`, `refactor/<주제>`, `docs/<주제>`, `tooling/<주제>`.
- 흐름: worktree에서 커밋 → `git push -u origin <브랜치>` → main 대상 PR(초안) → CI 녹색 → 병합 → `remove`.
- 벤치·프로파일은 PC 전체 CPU를 쓰므로, 다른 worktree에서 빌드가 도는 동안 잰 수치는 판정에 쓰지 않는다.

## 환경 메모

- 앱·MSBuild 빌드는 Windows + Visual Studio(v145 도구체인)가 필요하다. 클라우드 세션은 CMake/GCC로 코어·srcli·테스트만 빌드할 수 있다.
- Release의 LTCG(`/GL`)는 MSVC 내부 오류 때문에 꺼져 있다(BuildSettings.props 주석). 다시 켤 때는 4개 구성 테스트와 벤치를 함께 확인한다.

## CI와 PC 러너

- 빌드·테스트·`/analyze`·ASan·A/B 벤치는 GitHub 클라우드(Linux, Windows)에서 돈다. PC(self-hosted runner, 라벨 `renderer-pc`)는
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
