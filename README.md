<div align="center">

<h1>Software Renderer Project</h1>

<p><strong>GPU 래스터라이저 없이 CPU에서 직접 구현한 3D 그래픽스 파이프라인</strong></p>

<p>
  <strong>🇰🇷 한국어</strong> ·
  <a href="README.en.md">🇺🇸 English</a> ·
  <a href="README.ja.md">🇯🇵 日本語</a>
</p>

<p>
  <img alt="C++26 Preview" src="https://img.shields.io/badge/C%2B%2B-26%20Preview-00599C?logo=cplusplus">
  <img alt="CPU Renderer" src="https://img.shields.io/badge/renderer-CPU-7B61FF">
  <img alt="oneTBB" src="https://img.shields.io/badge/parallelism-oneTBB-FFC20A">
  <img alt="Windows" src="https://img.shields.io/badge/platform-Windows-0078D4?logo=windows">
</p>

<p>
  <a href="https://youtu.be/3StOC1_IjT4"><strong>▶ 한국어 영상 보기</strong></a>
  &nbsp;·&nbsp;
  <a href="https://youtu.be/AWwQuzhvvnM">日本語の解説映像</a>
</p>

</div>

---

DirectX 11을 사용하며 부족하다고 느꼈던 렌더링 파이프라인의 기본 원리를 직접 확인하기 위해 시작한 CPU 기반 소프트웨어 렌더러입니다. 모델 변환부터 클리핑, 래스터화, 깊이 테스트, 조명, 후처리와 화면 출력까지 직접 구현하고, 그 위에 SIMD와 타일 기반 멀티스레딩을 적용했습니다.

| 개발 형태 | 개발 기간 | 기술 환경 | 화면 출력 |
|:---:|:---:|:---:|:---:|
| **1인 개인 프로젝트** | 2025.07.07–2025.08.27 | C++26 Preview · MSVC v145 · Windows | WinAPI DIB · GDI |

## Preview

### 성능 비교

| 최적화 전 | 최적화 후 |
|:---:|:---:|
| ![최적화 전 8 FPS](https://github.com/user-attachments/assets/a8bf1116-44d3-42cb-b58f-d5229d874cd4) | ![최적화 후 58 FPS](https://github.com/user-attachments/assets/8cb37072-23fa-4ebc-9cae-0bb7e25ededb) |
| **8 FPS** | **58 FPS** |

### 디버그 시각화

| Wireframe | AABB | Normal Vector |
|:---:|:---:|:---:|
| <img src="https://github.com/user-attachments/assets/c22bb7bd-24b1-4339-894c-616426d648c9" alt="와이어프레임 디버그" width="100%"> | <img src="https://github.com/user-attachments/assets/ee8e5c99-7a6e-42c6-b56e-84313f51a172" alt="AABB 디버그" width="100%"> | <img src="https://github.com/user-attachments/assets/c3c06817-227d-4ac8-b756-f737de7136cb" alt="법선 벡터 디버그" width="100%"> |

### FXAA

| 적용 전 | 적용 후 |
|:---:|:---:|
| ![FXAA 적용 전](https://github.com/user-attachments/assets/1afcc928-aa3e-4001-b0e2-845a360d3c66) | ![FXAA 적용 후](https://github.com/user-attachments/assets/0c303273-3a2b-40f6-a8f7-178497ab5726) |

## 핵심 성과

| 항목 | 결과 |
|---|---|
| 렌더링 경로 | 정점 처리부터 픽셀 출력까지 **CPU에서 직접 구현** |
| 성능 비교 이미지 | **8 FPS → 58 FPS** |
| 병렬 처리 단위 | **16×16 tile** + oneTBB task scheduling |
| SIMD 경로 | **128-bit SSE** 기본 경로 + 런타임 **256-bit AVX** 선택 |
| 공간 가속 | Object AABB frustum culling + octree node culling |
| 디버그 도구 | Wireframe · Normal · AABB · Light Direction |
| 빌드 구성 | Debug/Release × Win32/x64 |

성능 이미지는 2025-08-27 당시의 비교 자료입니다. CPU, 해상도, 장면과 빌드 구성에 따라 결과가 달라지므로 표준화된 벤치마크 수치로 보지는 않습니다.

## 무엇을 보여주는 프로젝트인가

### 그래픽스 파이프라인

- Model-View-Projection 변환과 viewport mapping
- Sutherland–Hodgman frustum plane clipping
- Back-face culling과 reciprocal-depth buffer
- 고정소수점 edge function 기반 triangle rasterization
- 원근 보정 attribute interpolation
- Directional light 기반 Phong illumination
- OBJ/MTL 및 diffuse texture loading
- Bresenham/DDA line drawing과 FXAA

### 시스템·최적화

- Render queue와 16×16 tile binning
- oneTBB 기반 command 처리와 tile rasterization
- thread-local vertex cache, stamp, clip buffer와 triangle pool
- 자체 수학 라이브러리 `SRMath`의 SSE 연산
- CPUID/XCR0 검사 후 SSE/AVX 구현을 선택하는 runtime dispatch
- AABB와 octree를 이용한 coarse/fine frustum culling
- 이동 전용 GDI back-buffer RAII와 구조화된 asset error
- C++20/23/26 기능을 API 계약에 적용한 modern C++ 설계

## 렌더링 아키텍처

```text
SoftrendererProject.exe
├─ Framework
│  ├─ WinAPI window / input / frame loop
│  └─ Camera / GameObject update
├─ Asset Pipeline
│  ├─ OBJ / MTL parser
│  ├─ Texture loader
│  └─ Missing-normal generation
├─ Scene Submission
│  ├─ AABB / Frustum / Octree culling
│  └─ RenderQueue
└─ Renderer
   ├─ Vertex transform / back-face culling / clipping
   ├─ 16×16 tile binning
   ├─ Parallel rasterization / depth / Phong shading
   ├─ Optional FXAA
   └─ GDI DIB Present
```

```text
OBJ · MTL · Texture
→ Scene update
→ Frustum / Octree culling
→ RenderQueue
→ Vertex transform / clipping
→ Tile binning
→ Rasterization / depth test / shading
→ Optional FXAA
→ GDI Present
```

렌더 명령은 장면 객체에서 수집됩니다. 화면에 영향을 주는 삼각형은 타일별 bin으로 분배되고, 서로 다른 타일을 oneTBB worker가 병렬 처리합니다. 최종 픽셀은 32-bit DIB 백버퍼에 기록된 뒤 `BitBlt`로 표시됩니다.

## 문제 해결 사례

이 절은 결과만 나열하지 않고, 개발 중 마주친 문제와 원인, 해결 과정, 결과를 함께 기록합니다.

| 사례 | 발생한 문제 | 해결 핵심 |
|---|---|---|
| C++ 표준과 자원 수명 | 수동 초기화·해제와 raw handle이 실패 경로에 취약 | RAII, 명시적 변환, `/W4 /WX` |
| SIMD 수학 연산 | CPU renderer의 반복 벡터·행렬 연산 비용 | 자체 `SRMath`, SSE, 선택적 AVX |
| 클리핑 | 교차 정점 생성 시 순서와 winding 유지가 복잡 | ping-pong clip buffer + triangle fan |
| 근접 객체 | 투영 좌표가 화면·Z-buffer 범위를 벗어남 | 호출 스택 분석, clipping과 clamp |
| 누락 법선 | Teapot 일부 face에서 normal이 없어 shading 오류 | face normal 누적 후 smooth normal 생성 |
| MTL 이름 | 콜론이 포함된 material key lookup 실패 | 마지막 콜론 뒤 이름으로 fallback lookup |
| 멀티스레딩 | 단일 스레드 rasterization과 반복 할당이 병목 | tile rendering, TBB, TLS cache/pool |

### 1. C++ 표준 준수와 자원 수명

**문제:** 초기 구조에서는 `Init`/`Shutdown` 호출 순서와 raw WinAPI/GDI handle에 자원 수명이 의존했습니다. 초기화 도중 실패하거나 창이 resize되는 경로에서는 정상 종료 경로와 다른 정리 순서를 따라야 했고, 암시적 형변환과 외부 라이브러리 경고가 프로젝트 코드의 문제를 가릴 수 있었습니다.

**원인 분석:** 자원의 소유자와 해제 순서가 타입이 아니라 호출 규약과 주석에만 표현돼 있었습니다. 특히 DIB bitmap은 삭제 전에 DC에 선택돼 있던 원래 object를 복원해야 하므로 단순 raw pointer 하나로 수명을 표현할 수 없었습니다.

**해결:** 생성자와 소멸자에서 수명을 관리하고, 화면 DC·memory DC·DIB bitmap·이전 선택 object를 이동 전용 `Renderer::GdiBackBuffer`로 묶었습니다. 형변환은 `static_cast`와 필요한 경계의 `reinterpret_cast`로 명시했으며, 자체 코드는 `/W4 /WX`, vendor header는 external warning 경계로 분리했습니다.

**결과:** 중간 실패와 resize에서도 동일한 RAII 정리 규칙을 사용할 수 있게 됐고, 외부 코드의 경고를 제외한 프로젝트 소스는 경고를 오류로 취급하는 설정을 통과하도록 유지했습니다.

### 2. SIMD를 이용한 수학 연산 최적화

**문제:** 소프트웨어 렌더러는 정점마다 행렬 곱, 내적, 정규화 같은 수학 연산을 CPU에서 반복하므로 연산 비용이 전체 프레임 시간에 직접 누적됐습니다.

**원인 분석:** 3D 벡터와 4×4 행렬은 동일한 형태의 부동소수점 연산을 반복하기 때문에 스칼라 처리만으로는 CPU의 SIMD 실행 폭을 활용하지 못했습니다. 반대로 단일 `vec4`를 무조건 AVX로 넓히면 유효 데이터보다 register 폭이 더 커져 이점이 보장되지 않았습니다.

**해결:** SSE intrinsic을 사용하는 자체 수학 라이브러리 `SRMath`를 구현해 행렬 곱, 내적, 정규화 등의 기본 연산을 128-bit 경로로 처리했습니다. 두 `vec4`를 같은 행렬로 변환하는 batch만 별도 AVX translation unit에서 처리하고, CPUID의 AVX/OSXSAVE와 XCR0의 XMM/YMM 보존 상태를 검사해 런타임에 SSE 또는 AVX 함수를 선택했습니다.

**결과:** 단일 벡터에는 데이터 폭이 맞는 SSE를 유지하면서, 실제로 8개 float을 채울 수 있는 batch에서만 AVX를 사용할 수 있게 됐습니다. 전체 최적화 과정에서 수학 연산 병목을 줄였지만, 공개 FPS 차이를 SIMD 하나의 효과로만 해석하지는 않습니다.

### 3. 클리핑 과정의 정점 순서와 삼각형 복원

**문제:** Sutherland–Hodgman 알고리즘으로 삼각형을 여섯 frustum plane에 순서대로 자르면 교차점이 새로 생기고 정점 수가 변합니다. 이 과정에서 정점 순서가 흐트러지면 winding과 back-face 판정이 깨지고 잘못된 삼각형이 만들어질 수 있었습니다.

**원인 분석:** 각 plane의 출력 polygon이 다음 plane의 입력이 되기 때문에 한 buffer에서 읽고 쓰면 아직 처리하지 않은 정점을 덮어쓸 수 있습니다. 최종 결과도 항상 세 정점이라는 보장이 없어 다시 삼각형으로 나누는 과정이 필요했습니다.

**해결:** 입력과 출력을 교대하는 ping-pong clip buffer를 사용하고, 이전 정점과 현재 정점의 inside/outside 상태가 달라지는 지점에서 교차 정점을 삽입했습니다. 최종 polygon의 순서를 유지한 채 첫 정점을 기준으로 triangle fan을 구성했습니다. clip buffer는 최대 크기가 정해져 있어 고정 용량 저장소로 바꿨습니다.

**결과:** frustum 경계를 가로지르는 polygon도 winding을 유지하며 안정적으로 rasterizer에 전달할 수 있었고, 작은 clipping hot path의 반복 heap allocation도 제거했습니다.

### 4. 원근 투영 시 근접 객체의 범위 초과

**문제:** 카메라에 매우 가까운 객체를 렌더링할 때 투영된 좌표가 화면 범위를 크게 벗어나면서 Z-buffer의 유효 범위를 넘어서는 memory access 오류가 발생했습니다.

**원인 분석:** 디버거의 call stack과 memory watch로 잘못된 index가 만들어지는 경로를 추적했습니다. near plane 주변의 큰 투영 좌표가 rasterization bounding box까지 전달되고, 화면 범위로 제한되지 않은 index 계산이 depth/pixel buffer 접근에 사용되는 것이 원인이었습니다.

**해결:** clip-space frustum test와 polygon clipping을 적용하고, NDC와 tile 범위를 `std::clamp`로 화면 안에 제한했습니다. 최종 pixel write에도 경계 검사를 두어 buffer 밖 쓰기를 차단했습니다.

**결과:** 근접 객체와 화면 경계를 가로지르는 삼각형을 처리할 때 유효한 pixel 범위만 순회하게 되어 테스트 장면에서 범위 초과 접근을 제거했습니다.

### 5. 누락된 법선 벡터 보정

**문제:** Utah Teapot OBJ의 일부 polygon에 normal vector 데이터가 없어서 해당 영역의 Phong shading이 일관되지 않거나 잘못된 밝기로 보였습니다.

**원인 분석:** OBJ의 position과 index는 정상이었지만 일부 vertex가 유효한 `vn`을 갖지 않았습니다. 법선이 0 vector인 상태로 조명 계산에 들어가면 diffuse와 specular 방향을 올바르게 구할 수 없습니다.

**해결:** mesh 안의 모든 vertex가 유효한 normal을 갖는지 확인하고, 하나라도 누락되면 각 triangle의 두 edge를 외적해 face normal을 계산했습니다. 이 값을 세 vertex에 누적한 뒤 정규화해 smooth normal을 생성했으며, 퇴화 face에는 안전한 기본 방향을 사용했습니다.

**결과:** normal 정보가 완전하지 않은 OBJ도 별도 수정 없이 일관된 shading으로 표시할 수 있게 됐습니다.

### 6. 콜론이 포함된 MTL material 이름

**문제:** 일부 OBJ의 `usemtl` 이름에 콜론(`:`)이 포함돼 있을 때 MTL에서 읽은 material key와 정확히 일치하지 않아 color와 texture 정보를 찾지 못했습니다.

**원인 분석:** OBJ 쪽 이름은 `object-part:material` 형태였지만 MTL map에는 콜론 뒤의 실제 material 이름만 존재하는 경우가 있었습니다. 전체 문자열로만 lookup하면 fallback 없이 기본 material로 남았습니다.

**해결:** 먼저 전체 이름으로 lookup하고, 실패하면 마지막 콜론 위치를 찾아 그 뒤의 substring으로 material map을 다시 검색했습니다.

**결과:** 콜론이 포함된 모델도 원래 의도한 material로 재매핑되어 color와 texture를 올바르게 불러올 수 있게 됐습니다.

### 7. 타일 기반 멀티스레딩 성능 최적화

**문제:** 모든 삼각형과 pixel을 한 스레드에서 처리하면 CPU core가 하나만 바쁘고, vertex shading과 clipping 과정의 임시 vector 재할당도 frame time을 증가시켰습니다.

**원인 분석:** rasterization은 화면의 서로 다른 영역을 독립적으로 처리할 수 있지만 기존 구조에는 안전한 작업 분할 단위가 없었습니다. 또한 매 frame 생성되는 임시 container가 capacity를 반복 확장하며 할당 비용을 만들었습니다.

**해결:** 화면을 16×16 tile로 나누고, triangle의 screen-space AABB를 이용해 영향을 받는 tile bin에 참조를 등록했습니다. 각 tile은 oneTBB task로 병렬 처리하고, vertex result·stamp·clip buffer·triangle pool은 thread-local storage에서 재사용했습니다. `reserve()`, high-watermark cache와 ping-pong buffer로 재할당을 줄였습니다.

**결과:** 개발 당시 단일 스레드 약 5 FPS에서 멀티스레드 약 60 FPS까지 향상된 측정이 있었고, 2025-08-27에 남긴 비교 이미지에서는 8 FPS에서 58 FPS로 향상됐습니다. 두 수치는 측정 시점과 조건이 다른 개발 기록이며 표준화된 벤치마크는 아닙니다.

<details>
<summary><strong>C++26 현대화 설계 자세히 보기</strong></summary>

### Modern C++ 적용

| 이전 방식 | 현재 방식 | 변경 이유 |
|---|---|---|
| raw pointer + 별도 길이 | `std::span`, fixed-extent span | buffer 길이와 비소유 계약을 타입에 포함 |
| C 배열과 `#define` | `std::array`, `inline constexpr`, scoped enum | 크기·타입 보존과 macro 오염 방지 |
| `stringstream`, `stoi` | `std::string_view`, `std::from_chars` | 임시 문자열·locale·예외 비용 없이 오류 위치 전달 |
| loader 내부 `MessageBox`/예외 | `std::expected<T, AssetLoadError>` | parsing과 WinAPI UI 분리 |
| template 내부 `static_assert` | C++20 concepts와 `sr.math` module | 잘못된 타입을 호출 지점에서 진단 |
| clipping용 `std::vector` | `FixedCapacityVector` / `std::inplace_vector` 호환 경로 | 고정 크기 hot path의 heap allocation 제거 |
| raw GDI handle 수동 정리 | 이동 전용 `Renderer::GdiBackBuffer` | 선택 object 복원 → bitmap 삭제 → DC 삭제 순서 보장 |
| 단일 SSE 경로 | CPUID/XCR0 runtime dispatch + 별도 AVX translation unit | 구형 CPU 경로를 유지하며 유효한 batch만 AVX 사용 |
| 겹치는 FXAA parallel write | `span` API + luminance/filter pass 분리 | 같은 값을 쓰더라도 발생하는 data race 제거 |

프로젝트는 `/std:c++latest`를 사용합니다. 아직 제공되지 않는 C++26 library 기능은 feature-test macro로 확인하고 동일한 계약의 fallback을 사용합니다. 최신 기능이 항상 빠르다는 이유로 교체한 것은 아니며, 단일 `vec4`처럼 SSE 폭이 정확히 맞는 연산은 그대로 유지합니다.

</details>

## 빌드와 실행

### 요구 사항

- Windows 10 또는 Windows 11
- Visual Studio 2026와 MSVC v145
- `Desktop development with C++` workload와 Windows SDK
- 최신 C++ 표준과 표준 라이브러리 module 지원

oneTBB 2023.1.0과 stb_image는 `src/ThirdParty`에 포함돼 있어 별도 package 설치가 필요하지 않습니다.

### Visual Studio

1. 저장소 루트의 `SoftrendererProject.sln`을 엽니다.
2. `Release | x64` 또는 필요한 구성을 선택합니다.
3. **Build > Build Solution**을 실행합니다.
4. **Debug > Start Without Debugging**으로 실행합니다.

지원 구성은 `Debug|x64`, `Release|x64`, `Debug|Win32`, `Release|Win32`입니다. 빌드 후 `assets`와 해당 architecture의 `tbb12.dll`이 출력 directory로 자동 복사됩니다.

### 명령줄

Visual Studio의 **Developer PowerShell**에서 실행합니다.

```powershell
msbuild .\SoftrendererProject.sln /m /p:Configuration=Release /p:Platform=x64
```

### 조작

| 입력 | 동작 |
|---|---|
| `W` / `A` / `S` / `D` | 카메라 이동 |
| 마우스 오른쪽 버튼 + drag | 카메라 회전 |
| `Space` | 모델 자동 회전 시작/정지 |
| `F1` | Anti-aliasing 끄기 |
| `F2` | FXAA 켜기 |

창 상단 menu에서는 Bresenham/DDA line drawing, None/FXAA, Normal Vector/AABB/Wire Frame/Light Direction 표시를 선택할 수 있습니다.

<details>
<summary><strong>디렉터리 구조 보기</strong></summary>

### 디렉터리 구조

```text
SoftwareRendererProject/
├─ assets/                 # OBJ, MTL, texture assets
├─ src/
│  ├─ Core/               # WinAPI application과 main loop
│  ├─ Graphics/           # Model, Mesh, Material, Texture, Octree
│  ├─ Math/               # SRMath, AABB, Frustum, SSE/AVX dispatch
│  ├─ Modules/            # sr.math C++ module interface
│  ├─ Platform/           # 최소 Win32 boundary header
│  ├─ Renderer/           # RenderQueue, clipping, tile rasterizer, FXAA
│  ├─ Scene/              # Camera와 GameObject
│  ├─ ThirdParty/         # oneTBB와 stb_image
│  └─ Utils/              # error type, fixed-capacity storage, timing
├─ SoftrendererProject.sln
└─ SoftrendererProject.vcxproj
```

</details>

## 설계 범위와 제한 사항

- Windows와 MSVC project system을 대상으로 합니다.
- OBJ face는 triangle과 quad를 지원하며, 5개 이상의 vertex를 가진 n-gon은 명시적 error로 반환합니다.
- texture sampling은 현재 nearest-neighbor filtering과 clamp addressing을 사용합니다.
- lighting은 directional light 기반 Phong model을 중심으로 구현돼 있습니다.
- 공개 FPS는 서로 다른 개발 단계의 기록이며 동일 조건의 정식 benchmark가 아닙니다.
- 현재 구조는 CPU rendering pipeline 학습과 최적화에 집중하며 GPU API backend나 범용 scene editor는 범위에 포함하지 않습니다.

## 외부 라이브러리

- [oneTBB 2023.1.0](https://github.com/uxlfoundation/oneTBB): task-based parallelism
- [stb_image](https://github.com/nothings/stb): image decoding
