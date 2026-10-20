# CLAUDE.md

CPU 소프트웨어 렌더러(C++23/26, oneTBB, SSE/AVX). 목표는 **기본 데모 장면을 1920×1080에서 최소 100fps**로 그리는 것이다(느린 프레임 포함).
이 파일은 AI 작업용 요약이다. 사람용 설명은 README(3개 언어), 상세 계약은 MAINTENANCE.md에 있다.

## 성능 기준 (100fps 판정)

- 측정 도구: `srcli bench` (창 없음, 고정 dt 1/60, 60프레임 warmup 뒤 600프레임).
- 역할 구분: `srcli bench`는 합격/불합격을 가르는 **판정용 숫자**, VTune·VS CPU Usage(`tests/CaptureCpuProfiles.ps1`)는 **어디가 느린지 찾는 진단용**이다.
  프로파일러는 측정 자체에 오버헤드가 있어 그 수치(ms)로 판정하지 않는다. 사용 순서는 아래 "최적화 작업 흐름" 2단계.
- 한 프레임 = `DemoScene::Update` + `RenderTarget::Clear` + `Renderer::RenderScene`. GDI 표시는 제외.
- 판정: Windows PC의 MSVC **Release x64** 빌드에서 `fill`, `wire`, `normal` 세 모드 모두 **`p95_ms <= 9`**.
  10ms(100fps)에서 GDI 표시 비용 여유 1ms를 뺀 값이다. `mean_ms`는 참고용이다.
- 잡음: 모드마다 3회 실행해 p95의 중앙값으로 판정한다(`./tests/Bench.ps1 -Runs 3`). 개선은 같은 조건에서 2% 이상 줄었을 때만 인정한다.
- 가끔 표시 포함 경로(`tests\bin\x64\Release\RendererTests.exe . --benchmark 600 1920 1080 fill none`)로 교차 확인한다.
- Linux/GCC나 클라우드 수치는 경향 확인용이고 판정에 쓰지 않는다.
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
--hidden-wire --dda --stats --threads N --target-fps F --require-target --json --out file.png`.
`--stats`는 패스별 시간(`prepare/bin/raster/line/post_ms`)과 카운터를 JSON에 넣지만 측정을 느리게 하므로 최종 수치에는 쓰지 않는다.

## 최적화 작업 흐름

한 번에 한 가지 변경만 하고, 아래 순서를 지킨다.

1. 기준 측정: `./tests/Bench.ps1 -Runs 3 -Label before-<주제>` (pixel_hash와 p95 기록).
2. 프로파일: `tests/CaptureCpuProfiles.ps1`(VS CPU Usage/VTune)로 핫스팟 함수를 찾고, 필요하면 해당 함수의 어셈블리(`/FAs` 또는 디스어셈블리)를 본다.
3. 수정: 핫스팟만 고친다. 어셈블리 수준(인라인 여부, 분기, SIMD 사용, 메모리 접근)까지 확인한다.
4. 테스트: `./tests/RunTests.ps1 -Quick` 전부 통과(골든 포함).
5. 재측정: `./tests/Bench.ps1 -Runs 3 -Label after-<주제>`.
6. 채택 조건: 같은 설정의 `pixel_hash`가 변경 전과 같고, p95 또는 mean이 2% 이상 줄었을 때만 남긴다. 아니면 되돌린다.
7. 커밋·푸시: 마무리 전 `./tests/RunTests.ps1 -Rebuild`(4개 구성) 통과 후, 커밋 메시지에 모드별 전후 mean/p95(ms)와 pixel_hash 동일 여부를 적고 푸시한다.

## 화면 확인

- `srcli render ... --out x.png`로 저장한 PNG를 직접 열어 본다.
- 골든: `tests/golden/{fill,wire,wire-hidden,normal,aabb,fxaa}.png` (320×180, warmup 30프레임).
  실패하면 `tests/out/golden/<이름>.actual.png`, `<이름>.diff.png`(다른 픽셀=빨강)가 생긴다.
- 허용 오차(채널 4, 픽셀 0.2%)는 `tests/GoldenTests.cpp` 상단에 있다. 의도한 변경일 때만
  `-UpdateGolden`(Windows) 또는 `tools/dev.sh golden`으로 갱신하고 PNG를 같이 커밋한다.

## 코드 지도

| 위치 | 내용 |
| --- | --- |
| `src/Renderer/Renderer.cpp` | 프레임 순서: prepareLights → prepareFrame → binCommands → renderTiles → renderLines → applyPostProcessing |
| `src/Renderer/RendererFrontend.cpp` | 정점 변환·캐시, 클리핑 호출, 16×16 타일 참조 |
| `src/Renderer/Rasterizer.cpp`, `RendererShading.cpp` | 삼각형 coverage·깊이·Hi-Z·선, Phong |
| `src/Renderer/RendererClipping.cpp` | 동차 클리핑 |
| `src/Scene/GameObject.cpp`, `src/Graphics/Octree.cpp` | transform, 가시성, RenderQueue 제출 |
| `src/Core/DemoScene.*` | 데모 장면(모델 표·카메라·광원). Framework·srcli·골든이 공유 |
| `src/Core/HeadlessRunner.*`, `tools/srcli/` | 창 없는 실행·측정·PNG |
| `src/Core/Framework.*`, `src/Platform/` | Win32 창·입력·GDI 표시 (Windows 전용) |
| `src/Math/` | SRMath(SSE), `SIMD_AVX.cpp`만 AVX로 컴파일 |
| `tests/` | `TestMain.cpp` 실행기, 영역별 `*Tests.cpp`, `golden/`, `fixtures/` |

## 규칙

- 공개 멤버 함수 PascalCase, 내부 함수·지역 변수 camelCase, 멤버 `m_camelCase`. `SRMath`의 snake_case는 유지.
- 주석은 한국어. 좌표계·수명·스레드 계약처럼 이름만으로 모호한 것만 적는다.
- MSVC `/W4 /WX`, GCC `-Wall -Wextra -Werror`. 경고 0을 유지한다. `src/ThirdParty`는 수정하지 않는다.
- 컴파일러 전용 문법(`__forceinline`, intrinsic 탐지)은 `src/Platform/Compiler.h`나 `SIMD.cpp`처럼 한곳에 감싼다.
- 새 `.cpp/.h`는 `build/RuntimeSources.props`(앱·테스트·srcli 공통)에 추가한다. CMake는 `src/` 아래 `.cpp`를 자동으로 모은다.
  새 테스트 파일은 `RendererTests.vcxproj`에도 추가하고, 테스트는 그 파일의 `Register...Tests`에 등록한다.
- `src/Modules/sr.math.ixx`(MSBuild)와 `src/Modules/sr.math.h`(CMake, `SR_NO_MODULES`)는 같은 concept을 담는다. 함께 고친다.
- 장면 구성을 바꾸면(`DemoScene.cpp`의 표) 벤치마크 기준과 골든이 바뀐다. 성능 작업과 섞지 않는다.
- 픽셀 소유권·같은 깊이에서 작은 primitive ID 우선·명령 순서 보존 같은 결정성 계약은 MAINTENANCE.md "변경할 때 지킬 계약"을 따른다.

## 문서

- `PERFORMANCE.md`(약 90KB)와 `OPTIMIZATION_REPORT.md`는 과거 프로파일링 기록이다. 필요한 절만 찾아 읽는다.
- 새 성능 결과는 `tests/Bench.ps1`의 jsonl을 근거로, 변경 전후 mean/p95/pixel_hash를 짧게 남긴다.
- 프로파일러 스크립트: `tests/CaptureCpuProfiles.ps1`(VS CPU Usage, VTune). 산출물은 `tests/bin/profiles`(git 제외).

## 환경 메모

- 앱·MSBuild 빌드는 Windows + Visual Studio(v145 도구체인)가 필요하다. 클라우드 세션은 CMake/GCC로 코어·srcli·테스트만 빌드할 수 있다.
- Release의 LTCG(`/GL`)는 MSVC 내부 오류 때문에 꺼져 있다(BuildSettings.props 주석). 다시 켤 때는 4개 구성 테스트와 벤치를 함께 확인한다.
