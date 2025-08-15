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
* **OBJ, MTL 파서 (OBJ, MTL Parser)**: std::fstream을 활용하여 .obj 파일 포맷을 읽고, **std::stringstream**을 이용해 각 줄의 데이터를 효율적으로 파싱하여 3D 모델 및 머테리얼 데이터를 로드하는 기능 구현.
* **래스터화 (Rasterization)**: 삼각형, 선 등 기본적인 기하 도형을 픽셀로 변환하는 핵심 과정 구현.
* **Z-버퍼링 (Z-Buffering)**: 깊이 테스트를 통해 올바른 객체 가시성을 보장하여 렌더링 오류를 방지.
* **텍스처 매핑 (Texture Mapping)**: 3D 모델 표면에 이미지를 입혀 사실감 향상.
* **퐁 일루미네이션 모델 (Phong Illumination Model)**: 광원, 시점, 법선 벡터를 고려하여 사실적인 표면 음영을 계산하는 셰이딩 모델 구현.
* **선그리기 알고리즘**: Bresenham 알고리즘과 DDA 알고리즘, 두 가지를 만들고 변경 가능하게 구현
* **클리핑 (Clipping)**: Sutherland–Hodgman algorithm을 이용한 Frustum Plane Clipping 구현으로 파이프라인 효율성 증대.
* **백 페이스 컬링 (Back-face Culling)**: 카메라를 등지고 있는 폴리곤을 제거하여 렌더링 부하를 줄이는 최적화 기법 적용.
* **프러스텀 컬링 (Frustum Culling)**: 화면 밖의 오브젝트는 그리지 않게 하여 최적화.
* **법선,AABB, 와이어프레임 표시 디버깅 (Normal, AABB, and Wireframe Visualization for Debugging)**: 정점 법선을 시각화하여 렌더링 오류 진단 및 디버깅 용이성 확보.
* **렌더 큐 및 타일 기반 렌더링 (Render Queue and Tile-based Rendering)**: 게임 오브젝트가 각자 렌더링이 필요하면 렌더큐에 넣고, 렌더러는 해당 렌더큐에서 렌더링에 필요한 정보를 얻은 후 어느 타일에 렌더링해야하는지 분배합니다. 그리고 분배 후 각 타일마다 OpenMP를 사용한 멀티스레드를 통해서 렌더링을 실행합니다. 
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
* **법선 벡터 오류:** Utah Teapot 모델에서 누락된 법선 벡터를 교차곱 연산으로 계산하는 로직을 구현했으며, 인덱스 반복 오류를 수정하여 문제를 해결했습니다.
* **MTL 파싱 오류**: material 이름에 콜론이 포함된 경우를 처리하도록 파싱 로직을 개선했습니다.
* **멀티스레딩 성능 최적화**: 멀티스레드에 유리하도록 타일 기반 렌더링으로 변경하고 레이스 컨디션, 메모리 재할당 등을 방지하기 위해 스레드마다 로컬 저장 vector를 생성하고 핑퐁 버퍼 및 reserve를 통해 push_back으로 일어나는 capacity 재할당을 막았습니다. 또한 OpenMP의 schedule(dynamic)/(guided) 등을 사용해 부하 분산을 최적화했습니다. 이를 통해 **단일 스레드에서 시작화면 기준 20FPS으로 돌아가는 렌더러의 성능을 멀티 스레드를 통해 45FPS**로 끌어 올렸습니다. 

---

## **결과물 (Output Screenshots/GIFs)**
![2025-08-14](https://github.com/user-attachments/assets/a8bf1116-44d3-42cb-b58f-d5229d874cd4)
<img width="1415" height="686" alt="스크린샷 2025-08-14 141501" src="https://github.com/user-attachments/assets/c22bb7bd-24b1-4339-894c-616426d648c9" />
<img width="1415" height="731" alt="스크린샷 2025-08-14 141532" src="https://github.com/user-attachments/assets/ee8e5c99-7a6e-42c6-b56e-84313f51a172" />
<img width="1408" height="724" alt="스크린샷 2025-08-14 141559" src="https://github.com/user-attachments/assets/c3c06817-227d-4ac8-b756-f737de7136cb" />
* Result by 08/14
