# SoftwareRendererProject

## **소개 (Introduction)**
이 프로젝트는 컴퓨터 그래픽스 파이프라인의 핵심 원리를 이해하고 직접 구현하기 위해 개발된 CPU 기반 소프트웨어 렌더러입니다. DirectX 11 경험을 통해 부족하다고 느꼈던 기본 원리에 대한 깊이 있는 학습과 최적화 기법(SIMD) 적용에 중점을 두었습니다.

---

## **프로젝트 목표 (Project Goal)**
* 컴퓨터 그래픽스 파이프라인의 핵심 원리 및 수학적 배경 심층 이해
* **SIMD (Single Instruction, Multiple Data) 명령어 활용**을 통한 벡터/행렬 연산 **성능 최적화 기법 습득 및 적용**
* 렌더링 파이프라인 각 단계에서의 오류 진단을 포함한 **디버깅 및 문제 해결 능력** 향상
* 로우 레벨 그래픽스 처리 과정에 대한 깊은 이해

---

## **기간 (Duration)**
2025/07/07 ~ 진행 중

---

## **주요 기능 (Key Features)**
* **MVP (Model-View-Projection) 변환**: 3D 공간의 객체를 2D 화면에 투영하기 위한 필수 행렬 변환 단계를 직접 구현.
* **OBJ 파서 (OBJ Parser)**: std::fstream을 활용하여 .obj 파일 포맷을 읽고, **std::stringstream**을 이용해 각 줄의 데이터를 효율적으로 파싱하여 3D 모델 데이터를 로드하는 기능 구현.
* **래스터화 (Rasterization)**: 삼각형, 선 등 기본적인 기하 도형을 픽셀로 변환하는 핵심 과정 구현.
* **Z-버퍼링 (Z-Buffering)**: 깊이 테스트를 통해 올바른 객체 가시성을 보장하여 렌더링 오류를 방지.
* **텍스처 매핑 (Texture Mapping)**: 3D 모델 표면에 이미지를 입혀 사실감 향상.
* **퐁 일루미네이션 모델 (Phong Illumination Model)**: 광원, 시점, 법선 벡터를 고려하여 사실적인 표면 음영을 계산하는 셰이딩 모델 구현.
* **선그리기 알고리즘**: Bresenham 알고리즘과 DDA 알고리즘, 두 가지를 만들고 변경 가능하게 구현
* **클리핑 (Clipping)**: Sutherland–Hodgman algorithm을 이용한 Near Plane Clipping 구현으로 파이프라인 효율성 증대.
* **백 페이스 컬링 (Back-face Culling)**: 카메라를 등지고 있는 폴리곤을 제거하여 렌더링 부하를 줄이는 최적화 기법 적용.
* **프러스텀 컬링 (Frustum Culling)**: 화면 밖의 오브젝트는 그리지 않게 하여 최적화.
* **법선 표시 디버깅 (Normal Visualization for Debugging)**: 정점 법선을 시각화하여 렌더링 오류 진단 및 디버깅 용이성 확보.
* **수학 함수 라이브러리 직접 구현 (Custom Math Library: SRMath.h)**: 행렬 및 벡터 연산 함수를 SIMD 명령어(SSE)를 활용하여 직접 구현, 성능 최적화와 기초 수학 이해도를 증명.

---

## **기술 스택 (Technologies Used)**
* **언어**: `C++17` (객체 지향 설계 및 최신 언어 기능 - 예: 구조적 바인딩 - 활용)
* **개발 환경**: `Visual Studio 2022`
* **OS 인터페이스**: `WinAPI` (창 관리, 사용자 입력 처리, 픽셀 버퍼 접근 등 OS 레벨 인터페이스 제어)
* **라이브러리**: `stb_image.h` (다양한 이미지 포맷 로딩 및 파싱을 위한 단일 헤더 라이브러리)

---

## **도전 과제 및 해결 방안 (Challenges & Solutions)**
* **SIMD 최적화**: 소프트웨어 렌더러의 핵심인 수학 연산(행렬, 벡터)은 성능에 결정적인 영향을 미칩니다. 프로젝트 초기 단계부터 이러한 연산의 효율성을 극대화하기 위해, **CPU의 SIMD(Single Instruction, Multiple Data) 명령어 세트인 SSE(xmminstrin.h)**를 활용하여 SRMath.h 라이브러리를 직접 설계하고 구현했습니다. 
* **클리핑의 복잡성**: Sutherland–Hodgman 알고리즘 구현 시, 다각형의 정점 순서 유지 및 새로운 정점 생성 로직의 복잡성 발생. 기존에 OBJ파서를 만들 때 사용했던 삼각분할(Triangulation)를 통해 성공적으로 구현했습니다.
* **원근 투영 시 근접 객체 렌더링 오류 해결**: 디버깅 시 콜 스택 및 메모리 감시를 통해 Z-버퍼 단계에서 x, y 좌표의 유효 범위 이탈로 인한 메모리 참조 오류를 파악하고, **std::clamp 함수를 이용한 값 고정(Clamping)**으로 성공적으로 문제를 해결했습니다.

---

## **결과물 (Output Screenshots/GIFs)**
<img width="1415" height="686" alt="스크린샷 2025-08-14 141501" src="https://github.com/user-attachments/assets/c22bb7bd-24b1-4339-894c-616426d648c9" />
<img width="1415" height="731" alt="스크린샷 2025-08-14 141532" src="https://github.com/user-attachments/assets/ee8e5c99-7a6e-42c6-b56e-84313f51a172" />
<img width="1408" height="724" alt="스크린샷 2025-08-14 141559" src="https://github.com/user-attachments/assets/c3c06817-227d-4ac8-b756-f737de7136cb" />
![2025-08-14 14-17-29 (1)](https://github.com/user-attachments/assets/c382b6f2-10c5-4435-a4a8-3632bc8f7c4d)
* Result by 08/14

---

## はじめに (Introduction)
このプロジェクトは、コンピューターグラフィックスパイプラインの核心原理を理解し、直接実装するために開発されたCPUベースのソフトウェアレンダラーです。DirectX 11の経験を通じて不足を感じていた基本原理の深い学習と、最適化手法（SIMD）の適用に重点を置きました。

---

## プロジェクト目標 (Project Goal)
* コンピューターグラフィックスパイプラインの核心原理および数学的背景の深層理解。
* **SIMD (Single Instruction, Multiple Data) 命令**を活用したベクトル・行列演算の**性能最適化手法の習得と適用**。
* レンダリングパイプラインの各段階におけるエラー診断を含む**デバッグおよび問題解決能力**の向上。
* 低レベルグラフィックス処理過程に対する深い理解。

---

## 期間 (Duration)
2025/07/07 ～ 進行中

---

## 主要機能 (Key Features)
* **MVP (Model-View-Projection) 変換**: 3D空間のオブジェクトを2D画面に投影するための必須の行列変換ステップを直接実装。
* **OBJパーサー (OBJ Parser)**: std::fstreamを活用して.objファイルフォーマットを読み込み、**std::stringstream**を利用して各行のデータを効率的にパースし、3Dモデルデータをロードする機能を実装。
* **ラスタライズ (Rasterization)**: 三角形や線などの基本的な幾何プリミティブをピクセルに変換する核心プロセスを実装。
* **Zバッファリング (Z-Buffering)**: 深度テストを通じて正確なオブジェクトの可視性を保証し、レンダリングアーティファクトを防止。
* **テクスチャマッピング (Texture Mapping)**: 3Dモデルの表面に画像を適用してリアリズムを向上。
* **フォンイルミネーションモデル (Phong Illumination Model)**: 光源、視点、法線ベクトルを考慮してリアルな表面シェーディングを計算するシェーディングモデルを実装。
* **線描画アルゴリズム**: BresenhamアルゴリズムとDDAアルゴリズムの2種類を実装し、切り替え可能に設定。
* **クリッピング (Clipping)**: Sutherland–Hodgmanアルゴリズムを用いたニアプレーンクリッピングを実装し、パイプラインの効率を向上。
* **バックフェースカリング (Back-face Culling)**: カメラから見て裏側にあるポリゴンを除去し、レンダリング負荷を軽減する最適化手法を適用。
* **法線表示デバッグ (Normal Visualization for Debugging)**: 頂点法線を視覚化し、レンダリングエラーの診断とデバッグを容易に。
* **カスタム数学ライブラリ (SRMath.h)**: 行列およびベクトル演算関数を**SIMD命令（SSE/AVX）**を活用して直接実装し、性能最適化能力と数学の基礎理解を証明。

---

## 使用技術スタック (Technologies Used)
* **言語**: `C++17` (オブジェクト指向設計および最新の言語機能、例: 構造化束縛、を活用)
* **開発環境**: `Visual Studio 2022`
* **OSインターフェース**: `WinAPI` (ウィンドウ管理、ユーザー入力処理、ピクセルバッファアクセスなどのOSレベルインターフェースを制御)
* **ライブラリ**: `stb_image.h` (様々な画像フォーマットの読み込みと解析のためのシングルヘッダーライブラリ)

---

## 課題と解決策 (Challenges & Solutions)
* **SIMD最適化**: ソフトウェアレンダラーの核心である数学演算（行列、ベクトル）は性能に決定的な影響を及ぼします。プロジェクトの初期段階からこれらの演算効率を最大化するため、**CPUのSIMD (Single Instruction, Multiple Data) 命令セットであるSSE (`xmmintrin.h`)** を活用して`SRMath.h`ライブラリを直接設計および実装しました。
* **クリッピングの複雑性**: Sutherland–Hodgmanアルゴリズムの実装において、多角形の頂点順序の維持と新しい頂点の生成ロジックに複雑さが発生しました。これをOBJパーサー作成時に使用した**三角分割（Triangulation）**を応用することで、問題なく実装しました。
* **遠近投影時の近接オブジェクトレンダリングエラーの解決**: デバッグ中に**コールスタックとメモリウォッチ**を使用し、Zバッファ段階で`x, y`座標が有効範囲を逸脱することによるメモリ参照エラーを特定しました。この問題は、**`std::clamp`関数を用いた値のクランプ(Clamping)**によって正常に解決しました。

---

## 成果物 (Output Screenshots/GIFs)
<img width="1415" height="686" alt="스크린샷 2025-08-14 141501" src="https://github.com/user-attachments/assets/c22bb7bd-24b1-4339-894c-616426d648c9" />
<img width="1415" height="731" alt="스크린샷 2025-08-14 141532" src="https://github.com/user-attachments/assets/ee8e5c99-7a6e-42c6-b56e-84313f51a172" />
<img width="1408" height="724" alt="스크린샷 2025-08-14 141559" src="https://github.com/user-attachments/assets/c3c06817-227d-4ac8-b756-f737de7136cb" />
![2025-08-14 14-17-29 (1)](https://github.com/user-attachments/assets/c382b6f2-10c5-4435-a4a8-3632bc8f7c4d)
* Result by 08/14


---

## Introduction
This project is a CPU-based software renderer developed to deeply understand and directly implement the core principles of the computer graphics pipeline. It focuses on in-depth learning of fundamental principles, which I felt were lacking from my DirectX 11 experience, and the application of optimization techniques (SIMD).

---

## Project Goal
* Deep understanding of the core principles and mathematical background of the computer graphics pipeline.
* Acquisition and application of **performance optimization techniques** for vector/matrix operations using **SIMD (Single Instruction, Multiple Data) instructions**.
* Enhancement of **debugging and problem-solving skills**, including diagnosing errors at each stage of the rendering pipeline.
* Profound understanding of low-level graphics processing.

---

## Duration
2025/07/07 ~ Ongoing

---

## Key Features
* **MVP (Model-View-Projection) Transformation**: Direct implementation of the essential matrix transformation steps to project 3D objects onto a 2D screen.
* **OBJ Parser**: Implemented functionality to load 3D model data by reading the .obj file format using std::fstream and efficiently parsing each line's data with **std::stringstream**.
* **Rasterization**: Implementation of the core process of converting basic geometric primitives like triangles and lines into pixels.
* **Z-Buffering**: Ensures correct object visibility through depth testing, preventing rendering artifacts.
* **Texture Mapping**: Enhances realism by applying images onto 3D model surfaces.
* **Phong Illumination Model**: Implemented a shading model that calculates realistic surface shading by considering light source, viewpoint, and normal vectors.
* **Line Drawing Algorithms**: Implemented two algorithms, Bresenham's and DDA, with the ability to switch between them.
* **Clipping**: Implementation of Near Plane Clipping using the Sutherland–Hodgman algorithm to improve pipeline efficiency.
* **Back-face Culling**: Application of an optimization technique that removes polygons facing away from the camera, reducing rendering load.
* **Normal Visualization for Debugging**: Visualizes vertex normals to facilitate rendering error diagnosis and debugging.
* **Custom Math Library (SRMath.h)**: Directly implemented matrix and vector operation functions leveraging **SIMD instructions (SSE/AVX)**, demonstrating performance optimization capabilities and a fundamental understanding of mathematics.

---

## Technologies Used
* **Language**: `C++17` (Utilizes object-oriented design and modern language features, e.g., Structured Bindings)
* **Development Environment**: `Visual Studio 2022`
* **OS Interface**: `WinAPI` (Controls OS-level interfaces for window management, user input handling, and pixel buffer access)
* **Library**: `stb_image.h` (Single-header library for loading and parsing various image formats)

---

## Challenges & Solutions
* **SIMD Optimization**: Mathematical operations (matrices, vectors) are critical for software renderer performance. From the early stages of the project, to maximize the efficiency of these operations, I designed and implemented the `SRMath.h` library using **SSE (Streaming SIMD Extensions) intrinsics (`xmmintrin.h`)** for CPU's SIMD instruction set.
* **Complexity of Clipping**: During the implementation of the Sutherland–Hodgman algorithm, complexities arose in maintaining polygon vertex order and generating new vertices. This was successfully implemented by leveraging **triangulation**, a technique previously used when developing an OBJ parser.
* **Fixing Near-Object Rendering Issues with Perspective Projection**: During debugging, by using the **call stack and memory watch**, I identified a memory access error in the Z-buffer stage caused by `x, y` coordinates exceeding their valid range. This issue was successfully resolved by **clamping these values using `std::clamp`**.

---

## Output Screenshots/GIFs
<img width="1415" height="686" alt="스크린샷 2025-08-14 141501" src="https://github.com/user-attachments/assets/c22bb7bd-24b1-4339-894c-616426d648c9" />
<img width="1415" height="731" alt="스크린샷 2025-08-14 141532" src="https://github.com/user-attachments/assets/ee8e5c99-7a6e-42c6-b56e-84313f51a172" />
<img width="1408" height="724" alt="스크린샷 2025-08-14 141559" src="https://github.com/user-attachments/assets/c3c06817-227d-4ac8-b756-f737de7136cb" />
![2025-08-14 14-17-29 (1)](https://github.com/user-attachments/assets/c382b6f2-10c5-4435-a4a8-3632bc8f7c4d)
* Result by 08/14

