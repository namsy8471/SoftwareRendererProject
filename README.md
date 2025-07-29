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
* **래스터화 (Rasterization)**: 삼각형, 선 등 기본적인 기하 도형을 픽셀로 변환하는 핵심 과정 구현.
* **Z-버퍼링 (Z-Buffering)**: 깊이 테스트를 통해 올바른 객체 가시성을 보장하여 렌더링 오류를 방지.
* **텍스처 매핑 (Texture Mapping)**: 3D 모델 표면에 이미지를 입혀 사실감 향상.
* **퐁 일루미네이션 모델 (Phong Illumination Model)**: 광원, 시점, 법선 벡터를 고려하여 사실적인 표면 음영을 계산하는 셰이딩 모델 구현.
* **선그리기 알고리즘**: Bresenham 알고리즘과 DDA 알고리즘, 두 가지를 만들고 변경 가능하게 구현
* **클리핑 (Clipping)**: Sutherland–Hodgman algorithm을 이용한 Near Plane Clipping 구현으로 파이프라인 효율성 증대.
* **백 페이스 컬링 (Back-face Culling)**: 카메라를 등지고 있는 폴리곤을 제거하여 렌더링 부하를 줄이는 최적화 기법 적용.
* **법선 표시 디버깅 (Normal Visualization for Debugging)**: 정점 법선을 시각화하여 렌더링 오류 진단 및 디버깅 용이성 확보.
* **수학 함수 라이브러리 직접 구현 (Custom Math Library: SRMath.h)**: 행렬 및 벡터 연산 함수를 SIMD 명령어(SSE/AVX)를 활용하여 직접 구현, 성능 최적화와 기초 수학 이해도를 증명.

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

![2025-07-29 01-58-27 (1)](https://github.com/user-attachments/assets/3db5a4bc-7764-4d8e-a99f-8185a08df974)
Result by 07/28

