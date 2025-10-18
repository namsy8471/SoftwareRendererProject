# SoftwareRendererProject

## **결과물 (Output Screenshots/GIFs)**
동영상 링크: [https://youtu.be/aXOsA8hP668](https://youtu.be/AWwQuzhvvnM)

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
  
* **OBJ, MTL 파서 (OBJ, MTL Parser)**: std::fstream을 활용하여 .obj 파일 포맷을 읽고, **std::stringstream**을 이용해 각 줄의 데이터를 효율적으로 파싱하여 3D 모델 및 머테리얼 데이터를 로드하는 기능 구현.  
  **OBJ・MTLパーサ**：`std::fstream`を用いて.objファイルを読み込み、`std::stringstream`を利用して効率的に解析し、3Dモデルおよびマテリアルデータをロード。  
  **OBJ/MTL Parser**: Implemented efficient 3D model and material loader using `std::fstream` and `std::stringstream`.
  
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
* **언어 / 言語 / Language**: `C++17`  
* **개발 환경 / 開発環境 / Development**: `Visual Studio 2022`  
* **OS 인터페이스 / OSインターフェース / OS Interface**: `WinAPI`  
* **라이브러리 / ライブラリ / Libraries**: `stb_image.h`, `TBB`

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



