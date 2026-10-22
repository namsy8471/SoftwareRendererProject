# CODEMAP.md

소스 파일 하나당 한 줄 안내. "이 기능은 어느 파일에 있나"를 파일을 열기 전에 찾는 용도다.
흐름·계층·스레드 계약은 [ARCHITECTURE.md](ARCHITECTURE.md), 변경 시 지킬 계약은 [MAINTENANCE.md](MAINTENANCE.md),
작업 명령과 판정 기준은 [CLAUDE.md](CLAUDE.md)에 있다. 여기에는 그 내용을 되풀이하지 않는다.

- 표기: `A.h` · `A.cpp`처럼 같은 줄에 묶인 파일은 선언/구현 짝이다. 굵은 이름은 그 파일의 핵심 타입·함수다.
- 범위: git이 추적하는 `.cpp/.h/.hpp/.ixx` 전부(`src/ThirdParty/` 제외). CI의 `tools/ci/check_codemap.py`가
  빠진 파일이나 없어진 파일이 있으면 실패한다. 파일을 추가·이동·삭제하면 이 표도 같이 고친다.
- 설명은 "무엇을 하는가"만 적는다. 줄 수·성능 수치처럼 자주 바뀌는 값은 적지 않는다.

## (루트)

| 파일 | 역할 |
| --- | --- |
| `SoftrendererProject.cpp` | Win32 앱 진입점 `wWinMain`. **Win32App**을 만들고 `Run()`만 호출한다 |
| `SoftrendererProject.h` | VS 템플릿이 만든 빈 헤더 |
| `resource.h` | 리소스 ID(메뉴·아이콘·대화상자). `SoftrendererProject.rc`와 짝, VS가 생성 |
| `targetver.h` | Windows SDK 버전 지정(VS 템플릿) |

## src/App/ (Windows 전용, CMake 빌드에서 빠짐)

| 파일 | 역할 |
| --- | --- |
| `Win32App.h` · `Win32App.cpp` | **Win32App**: 창·메뉴·키 입력·파일 드롭, 프레임 루프(`Run` → `runFrame`), 모델 비동기 로드 후 배치, 애니메이션 메뉴, GDI Present. 프레임 내용은 FrameRunner에 맡긴다 |
| `Win32AppProject.cpp` | Win32App의 프로젝트·씬 파일 메뉴(파일 대화상자, 저장 확인). 실제 읽기·쓰기는 ProjectSession |

## src/Core/ (앱·srcli·테스트가 공유하는 프레임 실행)

| 파일 | 역할 |
| --- | --- |
| `FrameRunner.h` · `FrameRunner.cpp` | **FrameRunner**: 플랫폼 무관 한 프레임(시작 훅 → `Scene::Update` → 수집 → Clear → `RenderScene`). Scene·Renderer·DebugFlags 소유 |
| `SceneRenderCollector.h` · `SceneRenderCollector.cpp` | **SceneRenderCollector**: 장면 → RenderQueue. 프러스텀/옥트리 컬링, 메시별 병렬 batch, 법선·AABB 디버그 선 |
| `DebugFlags.h` | **DebugFlags**: 디버그 표시 토글(법선, AABB, 와이어프레임, 광원 방향). SceneRenderCollector만 읽는다 |
| `DemoScene.h` · `DemoScene.cpp` | 벤치·골든 고정 장면. **`sr::demo::LoadScene`**(모델 표), **`FitCameraToScene`**/`FitCameraToBounds`. 바꾸면 벤치 기준·골든이 바뀐다 |
| `HeadlessRunner.h` · `HeadlessRunner.cpp` | 창 없는 실행. **ViewMode**(Fill/Wire/Normal/AABB/FXAA), **RunConfig**, **`Run()`** → FrameTimings·RunResult(픽셀 해시 포함) |

## src/Scene/ (장면 데이터, 렌더 명령을 모른다)

| 파일 | 역할 |
| --- | --- |
| `Scene.h` · `Scene.cpp` | **Scene**: 루트 GameObject 목록·광원·카메라·Animator. `Update`가 포즈 → transform → 스킨 순으로 갱신 |
| `GameObject.h` · `GameObject.cpp` | **GameObject**: transform 계층(부모부터 갱신, 순환 거부), Model/SkinnedMesh 참조, **EntityInfo**(씬 파일 id·이름·에셋) |
| `Camera.h` · `Camera.cpp` | **Camera**: 위치·yaw/pitch, 키 입력 이동, view/projection 행렬, `GetViewParams()` |

## src/Renderer/ (래스터라이저 본체)

| 파일 | 역할 |
| --- | --- |
| `Renderer.h` · `Renderer.cpp` | **Renderer**: `RenderScene` 프레임 순서(prepareLights → prepareFrame → binCommands → renderTiles → renderLines → applyPostProcessing), 작업자별 scratch, 타일 scatter |
| `RendererFrontend.cpp` | Renderer 앞단: 정점 변환·명령 캐시, 클리핑 호출, 16×16 타일 참조 생성 |
| `RendererClipping.cpp` | 동차 clip 공간 평면 클리핑과 속성 보간 |
| `RendererShading.cpp` | 원근 보정 보간, Phong 조명, 텍스처 샘플 |
| `Rasterizer.h` · `Rasterizer.cpp` | **Rasterizer**: 타일 하나의 삼각형 coverage·깊이·Hi-Z(16x16 색/깊이/ID 타일 버퍼에 그리고 EndTile에서 한 번 씀), 와이어/디버그 선 그리기(행 밴드) |
| `RenderTarget.h` · `RenderTarget.cpp` | **RenderTarget**: 빌린 색상 뷰(**PixelSurfaceView**, stride 있음) + 소유 depth/primitive ID(ID 면은 끌 수 있음, 앱/srcli는 끔), 연결 검증, Clear/ClearRect |
| `RenderQueue.h` | **RenderQueue**: 프레임의 MeshRenderCommand·DebugPrimitiveCommand 목록, 디버그 정점 버퍼 재사용 |
| `RenderCommand.h` | **MeshRenderCommand**, **DebugPrimitiveCommand**, DebugVertex, ERasterizeMode, DebugDepthPolicy |
| `RenderTypes.h` | **RenderOptions**, **RenderStats**, TileBounds, RasterPass, ELineAlgorithm, EAAAlgorithm, RenderFrameContext |
| `Tile.h` | `tileSize`(16) 상수, **PreparedTriangle**(클리핑 후 한 번 준비해 모든 타일이 공유) |
| `ShaderVertices.h` | **ShadedVertex**(월드/클립 좌표·법선·UV), RasterizerVertex |
| `DebugGizmos.h` · `DebugGizmos.cpp` | 광원 화살표, AABB 상자, 법선 선 디버그 primitive 생성 |
| `FXAA.h` · `FXAA.cpp` | `sr::fxaa::Apply`: 휘도 패스 + 필터 패스 후처리(입출력 분리, stride 허용) |

## src/Graphics/ (자산: 메시·재질·텍스처·로더)

| 파일 | 역할 |
| --- | --- |
| `Mesh.h` · `Mesh.cpp` | **Vertex**, **Mesh**(정점·인덱스·재질·Octree), `ComputeLocalAABB` |
| `Model.h` · `Model.cpp` | **Model**: Mesh 묶음 + 모델 AABB. ModelLoader만 채운다. `Model.cpp`는 빈 파일 |
| `Material.h` | **Material**: MTL/Phong 값(ambient·diffuse·specular·shininess·opacity)과 diffuse 텍스처 |
| `Light.h` | **DirectionalLight**(방향 = 광선 진행 방향) |
| `ViewParams.h` | **ViewParams**: 렌더러에 넘기는 view/projection/카메라 위치. Renderer가 Camera를 모르게 한다 |
| `Texture.h` · `Texture.cpp` | **Texture**: RGBA 저장, `Sample(u, v)`, **TextureWrap**(ClampToEdge/Repeat/MirroredRepeat) |
| `Octree.h` · `Octree.cpp` | **Octree**: 메시 삼각형 공간 분할, `ForEachVisibleNode`(프러스텀 질의) |
| `ModelLoader.h` · `ModelLoader.cpp` | **ModelLoader**: `LoadOBJ`(OBJ 파싱 → 인덱스 검증 → 누락 법선 → AABB/Octree), `FromMeshes`(glTF 등에 같은 후처리) |
| `TextureLoader.h` · `TextureLoader.cpp` | **TextureLoader**: `LoadImageFile`(코덱 → Texture), `LoadMTLFile`(재질, 같은 텍스처 공유) |
| `AssetParsing.h` | OBJ/MTL 공용 토큰·숫자 해석(`takeToken`, `splitCommand`, `parseNumber`) |
| `GltfLoader.h` · `GltfLoader.cpp` | `sr::gltf::LoadScene`/`BuildScene`: glTF 문서 → GameObject 트리·Mesh·재질, **LoadedScene**(skin·애니메이션 클립 포함) |

## src/Graphics/Codecs/ (자체 이미지 코덱, 메모리 → RGBA8)

| 파일 | 역할 |
| --- | --- |
| `ImageCodecs.h` · `ImageCodecs.cpp` | 공개 API: **DecodedImage**, **ImageFormat**, `DetectImageFormat`, `DecodeImage`(형식별 디코더로 분기), 픽셀 수 상한 |
| `CodecSupport.h` | 코덱 내부 공용 도우미(**ByteReader** 등). 코덱 구현 파일끼리만 쓴다 |
| `PngDecoder.cpp` | PNG 디코더(모든 색 형식·비트 깊이, Adam7, tRNS) |
| `Inflate.cpp` | DEFLATE/zlib 해제(PNG IDAT용) |
| `JpegDecoder.cpp` | 베이스라인 JPEG 디코더(IJG ISLOW IDCT, fancy 업샘플링, 재시작 마커) |
| `BmpDecoder.cpp` | BMP 디코더(OS/2·INFO·V2~V5 헤더) |
| `TgaDecoder.cpp` | TGA 디코더(RLE, 원점 방향) |
| `DdsDecoder.cpp` | DDS 디코더(비압축 마스크, BC1~BC5, BC7. 첫 면·첫 밉만) |
| `PngEncoder.cpp` | 8-bit RGB PNG 인코더(골든 이미지·srcli render 저장용) |

## src/IO/ (파일·JSON·glTF 문서·비동기 로드)

| 파일 | 역할 |
| --- | --- |
| `FileIO.h` · `FileIO.cpp` | `ReadWholeFile`(크기 상한), `WriteFileAtomically`(임시 파일 → rename) |
| `Json.h` · `Json.cpp` | 자체 JSON: **`json::Value`**, `Parse`(줄·열 오류), `Write` |
| `Gltf.h` · `Gltf.cpp` | glTF 2.0 문서(.gltf/.glb) 파싱·범위 검증: **Document**, Accessor·Primitive·Material 등, `ReadAccessorFloats`/`Indices`, `ReadImageBytes`, base64 |
| `BackgroundLoader.h` · `BackgroundLoader.cpp` | **BackgroundLoader**: 작업 스레드에서 로드, 완료분은 프레임 시작 훅의 `DrainCompleted`에서 메인 스레드로 적용 |

## src/Project/ (프로젝트 폴더와 씬 파일)

| 파일 | 역할 |
| --- | --- |
| `ProjectFiles.h` · `ProjectFiles.cpp` | `.srproj`/`.srscene` 형식: **ProjectDocument**, **SceneDocument**, EntityDesc, JSON 변환, 로드/저장, 경로 해석, `CreateProject`, `ImportModelAsset`, `CaptureScene` |
| `ProjectSession.h` · `ProjectSession.cpp` | **ProjectSession**: 열린 프로젝트 상태(dirty·busy), 비동기 열기/저장/새 씬/모델 가져오기 |

## src/Animation/

| 파일 | 역할 |
| --- | --- |
| `Animator.h` · `Animator.cpp` | **Animator**: glTF 클립 재생(재생/정지·속도·반복·클립 전환) → node 로컬 TRS |
| `AnimationSampling.h` · `AnimationSampling.cpp` | `SampleTrack`: STEP/LINEAR/CUBICSPLINE 키프레임 샘플링(회전은 slerp) |
| `Skinning.h` · `Skinning.cpp` | **SkinnedMesh**, **SkinInfluence**: CPU 선형 블렌드 스키닝, `SkinVertices`(CPUID로 스칼라/AVX 선택) |
| `SkinningAVX.cpp` | `SkinVerticesAVX`. 이 파일만 AVX로 컴파일 |

## src/Math/

| 파일 | 역할 |
| --- | --- |
| `SRMath.h` | 수학 진입점(외부는 이것만 include). Vector/Matrix/FixedPoint를 모은다 |
| `Vector.h` | SSE `Vector<2/3/4>` = **vec2/vec3/vec4**, Color/Color4, 연산자 |
| `Matrix.h` | **mat3/mat4**, 변환·투영 행렬, `inverse`(MatrixError) |
| `FixedPoint.h` | **FixedPoint**: 고정소수점 값 타입(`FixedPointLike` concept 대상, 렌더 경로에서는 아직 안 씀) |
| `Quaternion.h` | **quat**: 곱, slerp/nlerp, 행렬·축각 변환 |
| `AABB.h` · `AABB.cpp` | **AABB**: 확장·교차·포함·행렬 변환 |
| `Frustum.h` · `Frustum.cpp` | **Frustum**/**Plane**: VP 행렬에서 6평면 추출, AABB 판정 |
| `SIMD.h` · `SIMD.cpp` | `transform_pair`(vec4 두 개를 한 번에 변환, AVX/SSE 런타임 선택), `avx_available` |
| `SIMD_AVX.cpp` | `transform_pair`의 AVX 구현. 이 파일만 AVX로 컴파일 |

## src/Modules/

| 파일 | 역할 |
| --- | --- |
| `sr.math.ixx` | MSBuild용 named module: 수학 concept(VectorLike, MatrixLike, FixedPointLike, 범위 concept) |
| `sr.math.h` | 같은 concept의 헤더판(CMake, `SR_NO_MODULES`). 두 파일은 함께 고친다 |

## src/Platform/

| 파일 | 역할 |
| --- | --- |
| `Compiler.h` | 컴파일러 전용 확장 매크로(`SR_FORCEINLINE` 등)를 한곳에 모음 |
| `Win32Headers.h` | `windows.h` 전 설정(`WIN32_LEAN_AND_MEAN`, `NOMINMAX`) |
| `GdiPresenter.h` · `GdiPresenter.cpp` | **GdiPresenter**: DIB 크기 변경, 렌더 타깃에 빌려줄 뷰, `Present`(BitBlt + GdiFlush) |
| `GdiBackBuffer.h` · `GdiBackBuffer.cpp` | **GdiBackBuffer**: 메모리 DC/DIB 소유 RAII(선택 객체 복원 후 해제) |

## src/Utils/

| 파일 | 역할 |
| --- | --- |
| `AssetLoadError.h` | **AssetLoadError**: 메시지·경로·행 번호. 로더 공통 오류 타입 |
| `FixedCapacityVector.h` | `sr::FixedCapacityVector`: `std::inplace_vector`가 없을 때 쓰는 고정 용량 버퍼 |
| `Jobs.h` | `sr::jobs`: 병렬 작업 API(`ParallelFor` Adaptive/Fixed 분할, `PerWorker<T>` 스레드별 작업 공간, `ConcurrencyLimit`, `MaxConcurrency`, `IsolatedRunner`). 호출 측은 스케줄러를 직접 include하지 않는다 |
| `ImageFile.h` · `ImageFile.cpp` | 골든·srcli용 이미지: `CopySurface`, `WritePng`/`ReadPng`, `HashPixels`(픽셀 해시), `Compare`(허용 오차 diff) |
| `PerformanceAnalyzer.h` · `PerformanceAnalyzer.cpp` | **PerformanceAnalyzer**: Win32App의 프레임 delta time과 초당 평균 FPS |
| `Utils.h` | `GetExecutableDirectory`, `MakeAssetPath`(실행 파일 옆 `assets/`) |

## tests/

| 파일 | 역할 |
| --- | --- |
| `TestMain.cpp` | RendererTests 실행기: `--filter`, `--list`, `--update-golden`, `--benchmark`, `--visibility-benchmark` |
| `TestHarness.h` | **TestRegistry**/TestCase/TestContext, 영역별 `Register...Tests` 선언 |
| `TestSupport.h` | 공용 검사 도구(예외 기반 check, **MemorySurface**) |
| `MathTests.cpp` | `math.*`: 벡터·행렬, vec 비교, 프러스텀 판정 |
| `AssetTests.cpp` | `assets.*`, `scene.*`: OBJ/MTL 로더, CRLF, 장면 계층, FrameRunner |
| `ImageCodecTests.cpp` | `codecs.*`: 형식별 fixture 해시, 손상·상한, PNG 왕복 |
| `GltfTests.cpp` | `gltf.*`: base64, wrap, 계층, 오류·견고성, 렌더 |
| `AnimationTests.cpp` | `animation.*`: 쿼터니언, 샘플링, 스키닝(스칼라 = AVX), Animator, 장면 연결 |
| `IOTests.cpp` | `io.*`: JSON 파싱·쓰기, 파일 함수, BackgroundLoader |
| `ProjectTests.cpp` | `project.*`: 프로젝트/씬 JSON 왕복, 형식 오류, 생성·가져오기, ProjectSession |
| `RendererCoreTests.cpp` | `renderer.*`: 클리핑, 작업자 격리, 명령 캐시, 타일 참조, 래스터 경로, Phong, FXAA |
| `RenderTargetTests.cpp` | `target.*`, `platform.*`: 연결 검증, stride, 선 행 밴드, presenter 수명 |
| `VisibilityTests.cpp` | `visibility.*`: 깊이·Hi-Z, 숨은선, 결정성, 병렬 디버그 제출 |
| `GoldenTests.cpp` | `golden.*`: 데모 장면을 작게 렌더해 `tests/golden/*.png`와 비교(허용 오차는 파일 상단) |
| `Win32AppTests.cpp` | `win32app.*`: 입력·메뉴·애니메이션/프로젝트 메뉴. MSBuild 테스트 빌드에서만 |

## tools/

| 파일 | 역할 |
| --- | --- |
| `srcli/SrCli.cpp` | `srcli bench`(프레임 시간, `--json`), `srcli render --out`(PNG). HeadlessRunner 위의 얇은 CLI |
