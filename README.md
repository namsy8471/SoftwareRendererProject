# SoftwareRendererProject

## **결과물 (Output Screenshots/GIFs)**
VideoLink(한국어 자막): https://youtu.be/3StOC1_IjT4  
VideoLink(日本語字幕）: [https://youtu.be/aXOsA8hP668](https://youtu.be/AWwQuzhvvnM)

![2025-08-14](https://github.com/user-attachments/assets/a8bf1116-44d3-42cb-b58f-d5229d874cd4)  
Before Optimization  (8 FPS)  
최적화 전 (8 FPS)  
最適化前 (8 FPS)  

![2025-08-27 15-41-54](https://github.com/user-attachments/assets/8cb37072-23fa-4ebc-9cae-0bb7e25ededb)  
After Optimization (58 FPS)  
최적화 후 (58 FPS)  
最適化後 (58 FPS)  

<img width="1415" height="686" alt="스크린샷 2025-08-14 141501" src="https://github.com/user-attachments/assets/c22bb7bd-24b1-4339-894c-616426d648c9" />

WireFrame Debug

<img width="1415" height="731" alt="스크린샷 2025-08-14 141532" src="https://github.com/user-attachments/assets/ee8e5c99-7a6e-42c6-b56e-84313f51a172" />

AABB Debug

<img width="1408" height="724" alt="스크린샷 2025-08-14 141559" src="https://github.com/user-attachments/assets/c3c06817-227d-4ac8-b756-f737de7136cb" />

Normal Vector Debug

<img width="1426" height="746" alt="스크린샷 2025-08-17 232015" src="https://github.com/user-attachments/assets/1afcc928-aa3e-4001-b0e2-845a360d3c66" />

Before Anti-Aliasing

<img width="1426" height="746" alt="스크린샷 2025-08-17 232025" src="https://github.com/user-attachments/assets/0c303273-3a2b-40f6-a8f7-178497ab5726" />

After Anti-Aliasing(FXAA)

* Result by 08/27/2025

---

---

## **소개 (Introduction / 紹介)**
이 프로젝트는 컴퓨터 그래픽스 파이프라인의 핵심 원리를 이해하고 직접 구현하기 위해 개발된 CPU 기반 소프트웨어 렌더러입니다. DirectX 11 경험을 통해 부족하다고 느꼈던 기본 원리(렌더링 파이프라인)에 대한 깊이 있는 학습과 최적화 기법(SIMD, Multi Thread) 적용에 중점을 두었습니다.  

このプロジェクトは、コンピュータグラフィックスパイプラインの基本原理を理解し、自ら実装することを目的として開発されたCPUベースのソフトウェアレンダラーです。DirectX 11の経験を通じて不足していると感じた基礎原理（レンダリングパイプライン）をより深く学び、SIMDおよびマルチスレッドによる最適化技術の適用に重点を置きました。  

This project is a CPU-based software renderer developed to understand and implement the core principles of the computer graphics pipeline. It focuses on deep learning of rendering fundamentals—identified as lacking from DirectX 11 experience—and applying optimization techniques such as SIMD and multi-threading.

---

## **프로젝트 목표 (Project Goal / プロジェクト目標)**
* 컴퓨터 그래픽스 파이프라인의 핵심 원리 및 수학적 배경 심층 이해  
  コンピュータグラフィックスパイプラインの基本原理と数学的背景の深い理解  
  In-depth understanding of the core principles and mathematical foundations of the graphics pipeline  
* **SIMD (Single Instruction, Multiple Data) 명령어 활용**을 통한 벡터/행렬 연산 **성능 최적화 기법 습득 및 적용**  
  **SIMD命令**を利用したベクトル・行列演算の**性能最適化手法の習得と適用**  
  Learning and applying **SIMD-based vector/matrix operation optimization techniques**  
* 렌더링 파이프라인 각 단계에서의 오류 진단을 포함한 **디버깅 및 문제 해결 능력 향상**  
  各レンダリング段階におけるエラーデバッグおよび問題解決能力の向上  
  Improving debugging and troubleshooting capabilities across all rendering pipeline stages  
* 로우 레벨 그래픽스 처리 과정에 대한 깊은 이해  
  ローレベルなグラフィックス処理過程に対する深い理解  
  Deep understanding of low-level graphics processing workflows  

---

## **기간 (Duration / 期間)**
2025/07/07 ~ 2025/08/27  

---

## **주요 기능 (Key Features / 主な機能)**
* **MVP (Model-View-Projection) 변환**: 3D 공간의 객체를 2D 화면에 투영하기 위한 필수 행렬 변환 단계를 직접 구현.  
  **MVP変換**：3D空間のオブジェクトを2D画面に投影するための行列変換を実装。  
  **MVP transformation**: Implemented matrix transformation to project 3D objects onto a 2D screen.
  
* **OBJ, MTL 파서 (OBJ, MTL Parser)**: `std::filesystem::path`로 경로를 받고, `std::string_view`와 `std::from_chars`로 복사·예외 없는 파싱을 수행하며, 실패는 `std::expected`로 반환합니다.
  **OBJ・MTLパーサ**：`std::filesystem::path`、`std::string_view`、`std::from_chars`を利用し、失敗は`std::expected`で返します。
  **OBJ/MTL Parser**: Uses `std::filesystem::path`, zero-copy `std::string_view`, `std::from_chars`, and structured `std::expected` errors.
  
* **래스터화 (Rasterization)**: 삼각형, 선 등 기본적인 기하 도형을 픽셀로 변환하는 핵심 과정 구현.  
  **ラスタライズ**：三角形や線などの基本図形をピクセルへ変換する中核処理を実装。  
  **Rasterization**: Implemented the core process that converts primitives such as triangles and lines into pixels.
  
* **Z-버퍼링 (Z-Buffering)**: 깊이 테스트를 통해 올바른 객체 가시성을 보장하여 렌더링 오류를 방지.  
  **Zバッファリング**：深度テストによって正しいオブジェクトの可視性を保証し、レンダリングエラーを防止。  
  **Z-Buffering**: Ensures correct object visibility via depth testing to prevent rendering errors.
  
* **퐁 일루미네이션 모델 (Phong Illumination Model)**: 광원, 시점, 법선 벡터를 고려하여 사실적인 표면 음영을 계산하는 셰이딩 모델 구현.  
  **フォン照明モデル**：光源・視点・法線ベクトルを考慮したリアルな陰影計算モデルを実装。  
  **Phong Illumination Model**: Implemented realistic shading considering light sources, viewpoint, and normals.
  
* **선그리기 알고리즘 (Line Drawing Algorithms)**: Bresenham 및 DDA 알고리즘을 모두 구현하고 선택적으로 전환 가능하게 구성.  
  **線描画アルゴリズム**：BresenhamとDDAの両方を実装し、切り替え可能に設計。  
  **Line Drawing Algorithms**: Implemented both Bresenham and DDA algorithms with runtime switching.
  
* **클리핑 (Clipping)**: Sutherland–Hodgman 알고리즘을 이용한 Frustum Plane Clipping 구현으로 파이프라인 효율성 증대.  
  **クリッピング**：Sutherland–Hodgmanアルゴリズムによるフラスタム平面クリッピングを実装し、パイプライン効率を向上。  
  **Clipping**: Implemented Frustum Plane Clipping using the Sutherland–Hodgman algorithm for pipeline efficiency.
  
* **백 페이스 컬링 (Back-face Culling)**: 카메라를 등지고 있는 폴리곤을 제거하여 렌더링 부하를 줄이는 최적화 기법 적용.  
  **バックフェイスカリング**：カメラに背を向けたポリゴンを除去する最適化手法を適用。  
  **Back-face Culling**: Applied optimization technique removing polygons facing away from the camera.
  
* **프러스텀 컬링 (Frustum Culling)**: 메시의 AABB를 탐지하여 화면 밖의 오브젝트는 그리지 않게 함으로써 최적화.  
  **フラスタムカリング**：メッシュのAABBを検出し、画面外のオブジェクトを描画しないことで最適化。  
  **Frustum Culling**: Optimized by skipping objects outside the camera frustum based on AABB detection.
  
* **법선, AABB, 와이어프레임 시각화 (Normal, AABB, Wireframe Visualization)**: 렌더링 오류 진단을 위해 디버깅 시 시각화 지원.  
  **法線・AABB・ワイヤーフレームの可視化**：デバッグ時のレンダリングエラー診断を支援。  
  **Normal, AABB, and Wireframe Visualization**: Aids debugging by visualizing geometry and bounding boxes.
  
* **렌더 큐 및 타일 기반 렌더링 (Render Queue and Tile-based Rendering)**: 각 오브젝트의 렌더 데이터를 큐에 넣고, 타일 단위로 분배하여 TBB 기반 멀티스레딩 수행.  
  **レンダーキューとタイルベースレンダリング**：オブジェクトの描画データをキューに格納し、タイル単位で分配してTBBによるマルチスレッド処理を実行。  
  **Render Queue and Tile-based Rendering**: Queued object data and executed tile-level multi-threaded rendering using TBB.
  
* **FXAA 구현 (FXAA Implementation)**: 빠른 성능과 양호한 품질을 갖춘 안티앨리어싱 기법 구현.  
  **FXAA実装**：高性能かつ良好な品質のアンチエイリアス手法を実装。  
  **FXAA Implementation**: Implemented a fast anti-aliasing method with good visual quality.
  
* **수학 라이브러리 (Custom Math Library: SRMath.h)**: SSE 명령어를 활용해 벡터/행렬 연산 함수를 직접 구현.  
  **数学ライブラリ（SRMath.h）**：SSE命令を利用してベクトル・行列演算関数を自作。  
  **Custom Math Library (SRMath.h)**: Implemented vector/matrix functions using SSE for performance optimization.  

---

## **조작 방법 (Controls / 操作方法)**
* **F1**: 안티앨리어싱 OFF / アンチエイリアス OFF / Anti-aliasing OFF  
* **F2**: FXAA ON / FXAA ON / FXAA ON  
* **WASD**: 카메라 이동 / カメラ移動 / Camera movement  
* **마우스 우클릭 + 드래그**: 카메라 회전 / マウス右クリック＋ドラッグ：カメラ回転 / Camera rotation  
* **스페이스 바**: 모델 회전 정지/시작 / モデル回転の停止／開始 / Toggle model rotation  

---

## **기술 스택 (Technologies Used / 使用技術)**
* **언어 / 言語 / Language**: `C++26 preview` (`/std:c++latest`)
* **개발 환경 / 開発環境 / Development**: `Visual Studio 2026` / MSVC v145
* **OS 인터페이스 / OSインターフェース / OS Interface**: `WinAPI`
* **라이브러리 / ライブラリ / Libraries**: `stb_image.h`, `oneTBB 2023.1.0`

## Build

Visual Studio 2026에서 솔루션을 열고 `Debug|x64`, `Release|x64`, `Debug|x86`, 또는 `Release|x86` 구성을 빌드합니다. 모든 구성은 `/std:c++latest`와 포함된 oneTBB 바이너리를 사용하며, 빌드 후 해당 아키텍처의 `tbb12.dll`을 출력 폴더로 복사합니다.

## C++26 현대화 설계

이 프로젝트에서 “C++26”은 단순히 `/std:c++latest`를 켠다는 뜻이 아닙니다. MSVC v145가 현재 구현한 최신 표준 기능을 실제 API 계약에 사용하고, 아직 구현되지 않은 C++26 기능은 feature-test macro로 감지한 뒤 동일 계약의 프로젝트 fallback을 사용합니다. 따라서 사용 중인 컴파일러가 기능을 추가하면 호출부를 바꾸지 않고 표준 구현으로 전환됩니다.

| 이전 방식 | 현재 방식 | 변경 이유 |
| --- | --- | --- |
| C++11 raw pointer + 별도 길이 | `std::span` 및 고정 extent span | 버퍼 길이를 타입에 포함해 범위 오류와 소유권 혼동 방지 |
| 일반 C 배열과 `#define` 상수 | `std::array`, `inline constexpr`, scoped enum | 크기와 타입을 보존하고 매크로 이름 오염 제거; `__m128` ABI overlay만 주석과 함께 예외 유지 |
| `stringstream`, `stoi` 예외 파싱 | `std::string_view`, `std::from_chars` | 임시 문자열·locale·예외 비용 없이 입력 오류 위치 반환 |
| 로더 내부 `MessageBox`/예외 | `std::expected<T, AssetLoadError>` | 파일 파싱과 WinAPI UI를 분리해 테스트 가능한 로더 구성 |
| 수동 템플릿 `static_assert` | named module의 C++20 `concept` | 잘못된 타입을 템플릿 본문이 아닌 호출 지점에서 진단 |
| 헤더 반복 전처리 | `sr.math.ixx`와 `import std;` | 안정된 수학 계약을 모듈 인터페이스로 시험 적용하고 매크로 누출 감소 |
| 고정 크기 클리핑용 `std::vector` | C++26 `std::inplace_vector` 호환 `FixedCapacityVector` | 작은 핫 패스에서 TLS 힙 할당 제거; MSVC 지원 시 표준 타입 자동 사용 |
| `memset`, `ZeroMemory` | 값 초기화와 ranges 알고리즘 | byte 단위 조작을 요소 타입을 이해하는 연산으로 교체 |
| float로 누적한 프레임 시간 | `std::chrono::steady_clock::duration` | 시계 단위를 타입으로 유지해 정밀도와 단위 안전성 확보 |
| raw GDI 핸들 3개 수동 정리 | 이동 전용 `Renderer::GdiBackBuffer` | 선택 객체 복원 → bitmap 삭제 → DC 삭제 순서를 RAII로 보장 |
| 하나의 SSE 경로 | CPUID/XCR0 런타임 디스패치 + SSE/AVX 별도 번역 단위 | 구형 CPU 호환성을 유지하며 두 `vec4` 배치만 256-bit AVX 사용 |
| packed texture 정수를 `float`로 변환 | `Texture::Sample`이 `SRMath::Color` 반환 | 픽셀 포맷·엔디언 오해로 텍스처가 흰색이 되던 오류 제거 |
| header-only FXAA와 겹치는 TBB 쓰기 | `span` API + 분리된 luminance/filter pass | 같은 값을 써도 data race인 동시 기록 제거, vendor 헤더를 `.cpp`로 격리 |
| 효과 없는 TLS 복사본 `reserve` | 실제 TBB thread-local 참조를 최초 사용 시 reserve | 의도만 있고 효력이 없던 사전 할당 수정 |
| 광범위한 `pch.h` 전이 include | 최소 `Platform/Win32Headers.h` + 직접 include | 자체 코드 의존성을 명시하고 WinAPI 매크로 노출 범위 축소 |

### 주요 현대화 함수

- `ModelLoader::LoadOBJ`: OBJ 경로를 받아 모델 또는 행 번호가 포함된 `AssetLoadError`를 반환합니다. `from_chars` 기반 face 파서는 `v`, `v/vt`, `v//vn`, `v/vt/vn`을 예외 없이 해석합니다.
- `TextureLoader::LoadMTLFile`: MTL 명령과 인수를 `string_view`로 나누고 숫자 변환 실패를 `expected`로 전달합니다.
- `AABB::Transform`: 여덟 코너를 두 개씩 묶어 `SIMD::transform_pair`에 전달합니다.
- `SIMD::avx_available`: CPUID의 AVX/OSXSAVE와 XCR0의 XMM/YMM 보존 상태를 함께 확인합니다.
- `SIMD::transform_pair`: 첫 호출에서 SSE 또는 AVX 함수 포인터를 한 번 선택하고 이후 같은 구현을 재사용합니다.
- `Renderer::GdiBackBuffer::create/reset`: 화면 DC, 메모리 DC, DIB bitmap 및 이전 선택 객체의 수명을 한 객체가 관리합니다.
- `Camera::Update`: `std::span<const bool, 256>`으로 키 입력 크기를 컴파일 시 계약화합니다.
- `Texture::Sample`: stb_image의 RGBA byte 배열에서 RGB 채널을 정규화해 typed color로 반환합니다.
- `sr::fxaa::Apply`: 입력/출력 `span`의 크기를 검증한 뒤 luminance 작성과 필터링을 별도 TBB pass로 실행합니다.
- `SRMath::inverse`: Gauss-Jordan 소거가 실패하면 임의 행렬이나 예외 대신 `expected<mat4, MatrixError>`를 반환합니다.

### C++11/14/17 코드에서 올린 이유

C++11의 RAII와 스마트 포인터는 계속 좋은 기반이지만, raw 포인터가 비소유 버퍼인지 단일 객체인지 표현하지 못하고 파싱 실패를 예외 또는 별도 bool로 나눠 처리해야 했습니다. C++17의 `filesystem`, `from_chars`, if-initializer와 C++20의 `span`, ranges, concepts는 이 모호함을 타입과 반환값에 기록합니다. C++23의 `expected`, `to_underlying` 및 C++26의 고정 용량 컨테이너 방향까지 적용해, 코드의 주석뿐 아니라 컴파일러가 계약을 검사하도록 바꿨습니다.

단, 최신 기능이 항상 더 빠르다는 이유로 교체한 것은 아닙니다. 단일 `vec4`는 128-bit SSE가 데이터 폭에 정확히 맞으므로 그대로 유지하고, 두 `vec4`를 동시에 변환하는 배치에만 256-bit AVX를 사용합니다. oneTBB와 stb_image도 자체 코드로 재작성하지 않고 외부 헤더/구현 경계에 격리했습니다.

### 이전 현대화 작업 회고

초기 전환은 `/std:c++latest`, `std::expected`, `std::span`, `sr.math.ixx`를 추가했지만 다음 부분이 부족했습니다.

- `sr.math`가 빌드만 되고 실제 호출부에서 import되지 않았습니다. 현재 `AABB.cpp`가 모듈을 import하고 `VectorLike` concept로 경계 검사를 제약합니다.
- AVX 구현 파일 이름이 `SIMD_AVX2.cpp`였지만 실제 명령과 프로젝트 옵션은 AVX 부동소수점 연산이었습니다. 파일명을 `SIMD_AVX.cpp`로 맞추고 AVX2가 필요하지 않은 이유를 주석화했습니다.
- `stringstream`, `stoi`, 로더 내부 `MessageBox`가 남아 있었습니다. 파서는 `from_chars/expected`로 바꾸고 UI 표시는 Framework 한 곳으로 이동했습니다.
- GDI 핸들은 정상 경로에서만 수동 해제됐습니다. 현재는 중간 실패와 resize에서도 RAII가 복원·해제를 수행합니다.
- 클리핑 버퍼가 작은 고정 데이터임에도 `vector`를 사용했습니다. `inplace_vector` feature test와 fallback을 추가했습니다.
- 값 반환형의 `const`, C 배열, `memset`, 매크로 상수가 남아 있었습니다. 값 의미론, `array`, ranges, `constexpr`로 정리했습니다.
- 텍스처의 packed pixel을 `float` 하나로 변환해 RGB 전체가 과포화될 수 있었습니다. `Texture::Sample`이 채널별 `Color`를 반환하도록 수정했습니다.
- FXAA의 이웃 luminance 준비가 TBB 작업 사이에서 같은 행을 중복 기록했습니다. 계산 pass를 분리해 읽기 전용 경계를 만들었습니다.
- `GameObject`의 TLS reserve가 참조가 아닌 복사본에 적용됐습니다. 실제 `local()` 벡터를 최초 사용 시 reserve하도록 옮겼습니다.
- `pch.h`가 PCH 비활성 상태에서도 표준 헤더를 전이 포함했습니다. 최소 Win32 경계 헤더로 교체하고 직접 의존성을 복구했습니다.

---

## **도전 과제 및 해결 방안 (Challenges & Solutions / 課題と解決策)**
### **1. C++ 표준 지키기 (Maintaining C++ Standards / C++標準の遵守)**
컴파일러의 경고를 철저히 점검하고, RAII(Resource Acquisition Is Initialization) 원칙을 지키는 방향으로 설계했습니다.  
`Init`이나 `ShutDown` 같은 수동 초기화 함수를 사용하지 않고, **생성자와 소멸자 내에서 메모리를 자동으로 할당 및 해제**하도록 구조를 변경했습니다.  
또한 모든 형변환은 암시적 변환을 피하고, C++ 표준에 맞는 `static_cast`, `reinterpret_cast`를 사용했습니다.  
외부 라이브러리(`TBB`)로 인한 경고는 제외하고, **프로젝트 내부의 경고를 0개로 유지**했습니다.  

コンパイラの警告を徹底的に確認し、RAII（Resource Acquisition Is Initialization）原則に従って設計しました。  
`Init`や`ShutDown`のような手動初期化関数を使用せず、**コンストラクタとデストラクタ内でメモリを自動的に確保および解放**する構造に変更しました。  
また、暗黙的な型変換を避け、C++標準である`static_cast`および`reinterpret_cast`を使用しました。  
外部ライブラリ（`TBB`）による警告を除き、**プロジェクト内の警告を0件に抑えました。**  

All compiler warnings were thoroughly reviewed and resolved.  
Following the RAII principle, classes were refactored to handle memory allocation and deallocation automatically within constructors and destructors, avoiding manual `Init`/`ShutDown` methods.  
All type conversions were made explicit using `static_cast` or `reinterpret_cast`.  
Except for unavoidable `TBB`-related warnings, the project achieved **zero internal compiler warnings**.  

---

### **2. SIMD 최적화 (SIMD Optimization / SIMD最適化)**
소프트웨어 렌더러의 핵심 성능은 수학 연산(행렬, 벡터)의 효율성에 크게 의존합니다.  
이를 극대화하기 위해, **CPU의 SIMD(Single Instruction, Multiple Data) 명령어 세트 SSE(`xmmintrin.h`)**를 사용해  
**SRMath.h**라는 전용 수학 라이브러리를 직접 설계·구현했습니다.  
이로써 행렬 곱, 내적, 정규화 등의 연산 속도를 비약적으로 향상시켰습니다.  

ソフトウェアレンダラーの性能は、行列・ベクトル演算の効率に大きく依存します。  
この効率を最大化するために、**CPUのSIMD命令セットSSE**（`xmmintrin.h`）を利用し、  
**SRMath.h**という独自の数学ライブラリを設計・実装しました。  
これにより行列積、内積、正規化などの演算速度が大幅に向上しました。  

The core performance of the renderer depends heavily on the efficiency of mathematical operations.  
To maximize it, I designed and implemented a custom math library **SRMath.h**,  
using **CPU SIMD instructions (SSE, `xmmintrin.h`)**.  
This achieved a significant boost in speed for matrix multiplication, dot products, and normalization.  

---

### **3. 클리핑의 복잡성 해결 (Clipping Complexity / クリッピングの複雑性の解決)**
Sutherland–Hodgman 알고리즘을 이용해 다각형 클리핑을 구현하는 과정에서,  
정점 순서 유지와 새로운 정점 생성 로직이 복잡하게 얽히는 문제가 발생했습니다.  
이를 해결하기 위해, OBJ 파서 제작 과정에서 활용했던 **삼각분할(Triangulation)** 로직을 결합하여  
정확한 정점 순서 유지와 버텍스 삽입 처리를 안정적으로 수행했습니다.  

Sutherland–Hodgmanアルゴリズムによるポリゴンクリッピングの実装中、  
頂点の順序保持と新しい頂点生成の処理が複雑化する問題が発生しました。  
OBJパーサー作成時に使用した**三角分割**（Triangulation）のロジックを組み合わせることで、  
正確な頂点順序の維持と安定した頂点生成を実現しました。  

During the implementation of polygon clipping via the Sutherland–Hodgman algorithm,  
issues arose in maintaining vertex order and generating new intersection vertices.  
By integrating the **Triangulation logic** developed for the OBJ parser,  
the clipping process was stabilized with proper vertex sequencing and insertion.  

---

### **4. 원근 투영 시 근접 객체 렌더링 오류 해결 (Perspective Depth Error Fix / 近接オブジェクト描画エラーの修正)**
근거리 객체 렌더링 중 Z-버퍼의 유효 범위를 벗어나 메모리 접근 오류가 발생했습니다.  
디버깅 중 콜 스택과 메모리 감시를 통해 원인을 추적하고,  
**`std::clamp` 함수를 이용한 좌표값 제한(Clamping)** 으로 문제를 완벽히 해결했습니다.  

近距離オブジェクトの描画時にZバッファの有効範囲を超えるメモリアクセスエラーが発生しました。  
コールスタックとメモリウォッチを使用して原因を特定し、  
`std::clamp`関数による座標値の固定（Clamping）で完全に解決しました。  

A memory access error occurred when rendering near-plane objects due to Z-buffer overflow.  
By tracing the call stack and monitoring memory values,  
the issue was resolved by applying **value clamping using `std::clamp`** to constrain valid coordinates.  

---

### **5. 법선 벡터 오류 (Normal Vector Error / 法線ベクトルの欠落修正)**
Utah Teapot 모델에서 일부 폴리곤의 법선 벡터 데이터가 누락되어 있었습니다.  
이에 따라 **Normal Vector가 존재하지 않는 경우, 외적을 통해 자동 계산하는 보정 로직**을 추가했습니다.  
이 방법으로 모든 모델의 셰이딩 일관성을 확보했습니다.  

Utah Teapotモデルでは一部のポリゴンで法線ベクトルが欠落していました。  
そのため、**法線が存在しない場合は外積を用いて自動的に補正するロジック**を追加しました。  
これによりすべてのモデルで一貫したシェーディングを実現しました。  

Some polygons in the Utah Teapot model lacked normal vectors.  
A **cross-product-based normal generation routine** was implemented to automatically compute missing normals,  
ensuring consistent shading across all models.  

---

### **6. MTL 파싱 오류 (MTL Parsing Error / MTL解析エラー)**
Material 이름에 콜론(`:`)이 포함된 경우, 파서가 컬러 정보를 올바르게 불러오지 못했습니다.  
이를 해결하기 위해, **문자열의 마지막 콜론 이후를 머티리얼 키로 재검색하여**  
정확한 재매핑 로직을 구현했습니다.  

MTLファイル内のマテリアル名にコロン(`:`)が含まれる場合、  
パーサーが正しい色情報を取得できない問題がありました。  
**文字列の最後のコロン以降をマテリアルキーとして再検索**するロジックを実装して解決しました。  

When material names contained colons (`:`),  
the parser failed to correctly retrieve color values from the MTL file.  
The issue was fixed by implementing a **re-parsing logic that searches for the last colon position**  
and reassigns materials correctly.  

---

### **7. 멀티스레딩 성능 최적화 (Multithreading Optimization / マルチスレッド性能最適化)**
렌더링 성능을 극대화하기 위해 **타일 기반 렌더링(Tile-based Rendering)** 방식을 도입했습니다.  
각 타일은 독립적인 스레드에서 처리되며, **TBB**(Task-Based Threading Building Blocks)를 사용하여  
부하를 자동으로 분산하도록 스케줄링했습니다.  

또한, **벡터 재할당**(capacity 재확장)으로 인한 성능 저하를 방지하기 위해  
각 스레드별 로컬 캐시를 두고, `reserve()`와 핑퐁 버퍼 구조를 통해 메모리 재할당을 최소화했습니다.  
일부 지역 변수를 클래스 멤버 변수로 이동시켜 반복적인 생성·삭제를 제거했습니다.  

이러한 최적화를 통해,  
**단일 스레드에서 5 FPS로 동작하던 렌더러가 멀티스레드 환경에서 60 FPS로 향상**되었습니다.  

レンダリング性能を最大化するため、**タイルベースレンダリング**方式を導入しました。  
各タイルは独立したスレッドで処理され、**TBB**(Task-Based Threading Building Blocks)を使用して  
負荷が自動的に分散されるようにスケジューリングしました。  

さらに、ベクタの**再確保**(capacity拡張)による性能低下を防ぐため、  
スレッドごとにローカルキャッシュを設け、`reserve()`およびピンポンバッファ構造で  
メモリ再割り当てを最小化しました。  
一部のローカル変数をクラスメンバに変更し、再生成を防ぎました。  

その結果、  
**シングルスレッド時5FPSだったレンダラーがマルチスレッド環境で60FPSまで向上しました。**  

To maximize rendering performance, a **tile-based rendering system** was implemented.  
Each tile is processed on a separate thread, and **Intel TBB** was used for automatic load balancing.  
Performance degradation from vector reallocations was mitigated by  
introducing thread-local caches, `reserve()` usage, and ping-pong buffer structures.  
Frequent object creation/destruction was eliminated by promoting temporary variables to class members.  

As a result,  
the renderer improved from **5 FPS (single-threaded)** to **60 FPS (multi-threaded)** execution.  

---



