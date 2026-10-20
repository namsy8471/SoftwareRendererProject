# Maintenance Guide / 유지보수 가이드 / 保守ガイド

[한국어](#한국어) · [English](#english) · [日本語](#日本語)

## 한국어

### 어디부터 읽을까?

| 관심사 | 진입점과 책임 |
| --- | --- |
| 프레임 실행 | `src/Core/Framework.cpp`: 창·입력 처리, 시간 측정, Update/Render/Present 조정 |
| 데모 장면·headless | `src/Core/DemoScene.cpp`: 모델 표·카메라·광원·갱신 순서(창 무관) / `HeadlessRunner.cpp`: 창 없는 프레임 실행·측정 / `tools/srcli`: 벤치마크·PNG CLI |
| 장면 | `src/Scene/GameObject.cpp`: 부모부터 transform 갱신 → 가시성 검사 → mesh별 병렬 batch 생성 → mesh 순서 병합 |
| 렌더 순서 | `src/Renderer/Renderer.cpp`: `prepareLights` → `prepareFrame` → `binCommands` → `renderTiles` → `renderLines` → `applyPostProcessing` |
| 정점과 타일 분배 | `RendererFrontend.cpp`: 프레임 한정 단일 키 정점·변환 행렬 캐시, world/clip 변환, 타일 참조 구성 |
| 클리핑 | `RendererClipping.cpp`: 동차 clip 공간의 평면 검사·다각형 자르기·속성 보간 |
| 픽셀 | `Rasterizer.cpp`: PreparedTriangle·coverage·깊이·선·Hi-Z / `RendererShading.cpp`: 원근 보간과 Phong 조명 |
| 버퍼 | `RenderTarget.*`: stride 있는 비소유 색상 뷰, 소유 depth/primitive ID, 연결 검증·Clear |
| 플랫폼 자원 | `GdiPresenter.*`: resize·DIB 뷰·Present / `GdiBackBuffer.*`: DC/DIB 소유, 선택 객체 복원, 이동 전용 RAII |
| OBJ/MTL | `ModelLoader.cpp`: 문장 → 인덱스 검증/메시 → 누락 법선 → AABB/Octree. `AssetParsing.h`는 토큰·숫자 해석만 공유 |
| 수학 | `SRMath.h`는 호환 진입점. `FixedPoint.h`, `Vector.h`, `Matrix.h`에 구현을 나눔 |
| 빌드 | `build/BuildSettings.props`: 공통 설정. `build/RuntimeSources.props`: 앱·테스트·srcli의 공통 소스 목록. `CMakeLists.txt`: headless 빌드(src/ 아래 .cpp 자동 수집) |

### 변경할 때 지킬 계약

- 공개 멤버 함수는 PascalCase, 내부 함수/지역 변수는 camelCase, 클래스 멤버는 `m_camelCase`를 쓴다. `SRMath`의 기존 snake_case와 컨테이너 호환 API는 유지한다. `.clang-format`을 적용하되 `src/ThirdParty`는 제외한다.
- 좌표계와 수명은 이름만으로 모호하면 한국어 주석을 남긴다. 열벡터 규약의 `world = parentWorld * local`, 법선의 역전치 변환, clip 공간과 screen 공간을 구분한다.
- `RenderQueue`의 mesh/material/index 참조는 빌린 데이터다. 동기 `RenderScene` 호출이 끝날 때까지 원본을 수정하거나 해제하지 않는다. 하나의 Renderer를 동시에 호출하지 않는다.
- 광원은 `prepareLights`에서 프레임당 한 번 `PreparedDirectionalLight`로 준비한다. 기존 `normalize(-1.f * rayDirection)` 식을 유지하며 Renderer 소유 배열의 capacity만 재사용한다. 같은 입력 주소라도 방향·색·개수는 매번 갱신하고, `RenderFrameContext::lights`는 모든 worker join까지 유효한 읽기 전용 span이다. 픽셀 값 보존과 달리 정규화의 FP 예외 발생 시점·스레드까지 같다고 가정하지 않는다.
- `Renderer::RenderScene(RenderTarget&, ...)`는 프레임 동안만 target을 빌린다. `PixelSurfaceView`의 stride는 픽셀 단위이며 색상은 top-down BGRX, depth/ID는 별도 tight 배열이다. resize 전에 `Detach`하고 새 DIB를 `Attach`한다. 실패는 빈 상태로 남고, 한 축이 0인 target은 정상 no-op이다. color padding을 Clear/FXAA로 덮어쓰지 않는다.
- `WorkerScratch`는 Renderer 인스턴스별 oneTBB TLS다. 삼각형 풀은 `concurrent_vector`, 타일 참조는 flat `tileReferences`, 타일 메타데이터는 `tileScatter`의 count/cursor로 관리한다. `touchedTileIds`를 먼저 등록하고 참조 삽입 성공 후 count를 증가시킨다. `BeginFrame`은 이전 크기의 touched 메타데이터와 flat 참조를 풀보다 먼저 비우며, viewport에 맞춘 메타데이터 resize는 필요할 때 수행한다. Front-end join 뒤 활성 worker 목록과 prefix offset으로 겹치지 않는 목적 구간을 정하고 worker별 병렬 scatter를 실행한다. Scatter join 후 각 tile span을 선택적으로 정렬하고 래스터화하며 픽셀 소유권과 primitive ID는 유지한다. 기본 빠른 X-ray만 있으면 flat 타일 참조·worker 메타데이터의 신규 할당과 depth prepass를 생략한다. 별도 JobSystem은 없다.
- 정점·변환 캐시는 worker별 단일 entry이며 현재 프레임에서만 유효하다. `BeginCachedCommand`의 키는 `sourceMesh` identity, `vertices.data()/size()`, world와 viewProjection의 각 16개 float 비트, rasterize mode다. 행렬 객체 padding이나 근사값을 비교하지 않는다. Material과 index 부분집합은 정점 속성 계산에 영향을 주지 않아 키에서 제외한다. 정확한 키 일치와 충분한 두 배열 크기를 확인한 hit만 stamp·정점 결과·MVP·법선 행렬을 재사용한다. `BeginFrame`은 다른 초기화보다 먼저 키를 무효화하므로 같은 주소의 정점 수정·자산 재로드 결과를 프레임 간 재사용하지 않는다. 기존 `BeginCommand(size_t)`는 항상 키를 무효화하고 stamp를 전진시키며, cache miss도 이 경로를 쓴다. Stamp wrap 때 기존 stamp를 지우고 정점·stamp 배열은 각각 부족한 크기를 복구한다. 할당 실패 시 키는 무효 상태로 남고, 두 저장소 확보와 행렬 계산이 모두 성공한 뒤에만 새 키를 공개한다. 전역 map/LRU나 프레임 간 캐시는 두지 않으며 ID·작업 분배·셰이딩 수식은 유지한다.
- 타일별 vector의 capacity 누적 대신 worker별 flat 참조와 최종 포인터 배열을 재사용한다. 참조 복사·prefix·scatter 비용은 별도로 평가한다. `binReferences`는 논리 참조를 한 번만 세며, `retainedBinBytes`는 flat/최종/wire/debug 저장소, count/cursor/offset, 헤더, touched/활성 목록을 포함한다. `binMs`는 front-end 참조 준비, `rasterMs`는 prefix/scatter/정렬도 포함하므로 이전 통계와 범위를 구분한다. 속도·메모리 비교는 진행 중이며 성능 향상이나 최종 채택은 확정하지 않는다.
- 클리핑 결과는 `PreparedTriangle`로 한 번 준비해 공유한다. 원본 삼각형과 fan 순번의 ID는 안정된 command 순서에 의존하므로 메시 batch를 worker 완료 순서로 합치지 않는다. 정확히 같은 깊이는 작은 ID가 이긴다. 정렬 유무가 이 규칙을 바꾸면 안 된다.
- 채움 패스는 서로 겹치지 않는 `[min, max)` 타일에만 쓴다. 기본 `hiddenWireframe=false`는 빠른 X-ray 와이어이며, 와이어 면은 깊이를 쓰거나 채움 면을 가리지 않는다. 옵션 ON이면서 와이어 명령이 있을 때만 모든 면의 depth-only 패스를 선행한다. 법선은 독립 DepthTested 정책으로 기존 면 깊이를 읽는다. OFF에서는 채움 면 깊이만 존재하므로 순수 와이어 장면의 법선은 면에 가려지지 않는다. 와이어는 disjoint Y행 band에서 병렬 처리하고 join 뒤 법선을 처리한다. DepthTested 선이 256개 이상이면 256-line chunk로 병렬 준비하고 원본 순서를 보존하는 bin·행 band로 그리며, 작은 묶음은 순차 fallback을 쓴다. AABB/광원 overlay는 법선 join 뒤 순차 실행한다.
- 와이어 band는 대략 64행 기준 최대 32개이며 선은 depth/primitive ID를 바꾸지 않는다. `BeginLineRows(minY, maxY)`는 화면으로 clamp한 반개구간을 지정하고 빈 band는 즉시 반환한다. Bresenham은 끝점을 band 경계로 다시 자르지 않는다. `restrictBresenhamRows`가 원래 major-axis 단계에서의 좌표와 error 위상을 복원해 band에 해당하는 첫 단계부터 마지막 단계까지만 실행하며, tie 규칙과 원래 끝점 기반 깊이 보간을 유지한다. Error 누적·두 배 계산은 `int64`로 처리해 허용된 극단 끝점에서도 기존 `int32` 중간 overflow를 피한다. DDA는 원래 순차 덧셈 진행을 그대로 유지한다. 원래 점화식과 비교하는 `independentLineTraversalTests`를 포함해 Debug/Release × Win32/x64의 증분 빌드·런타임 테스트가 경고·오류 없이 통과했고 x64 Debug ASan 검증도 포함했다. 이는 속도 향상을 뜻하지 않는다.
- 큰 DepthTested 선 묶음(256개 이상)은 준비 optional 배열을 먼저 `clear()`하지 않고 `resize(totalLines)`로 논리 크기를 정확히 맞춘다. 병렬 준비는 모든 슬롯에 이번 프레임의 유효 값 또는 `nullopt`를 반드시 대입하고, join 뒤에만 bin 구성·그리기가 읽는다. 작은 묶음과 빈 debug 프레임은 준비 배열을 `clear()`해 논리 크기를 0으로 만들며 commands/chunks/bins는 매 프레임 계속 비운다. 준비 중 예외가 나면 bin 구성·그리기로 진행하지 않고 다음 프레임의 전체 덮어쓰기 또는 clear로 복구한다. `preparedDebugSlotFreshnessTests`는 769→256→257→255→0→769 전환에서 유효/무효 슬롯과 색상 등의 잔류를 검사한다. 재사용의 성능 효과는 별도 측정 대상이다.
- front-to-back/Hi-Z는 독립 옵션이며 기본 꺼짐이다. Hi-Z는 큰 `1/w`가 가까운 규약에서 완전 coverage의 최소 역깊이를 사용하고 동일 깊이·구멍·불확실한 경계는 기각하지 않는다. 타일 worker 간 요약을 공유하거나 이전 프레임 깊이를 읽지 않는다.
- `collectStats`는 선택적이다. 패스 시간, 준비 삼각형/bin 참조, coverage/depth/shading, Hi-Z/선 기각, 메모리를 타일별로 모아 join 이후 합산한다. 픽셀마다 atomic을 넣지 않으며 최종 성능 비교에서는 상세 계측을 끈다.
- `AddChild`는 shared_ptr로 관리되는 부모만 허용하고, 기존 부모·순환·중복 연결을 거부한다. 자식은 부모를 weak_ptr로 참조한다. 부모 AABB는 자식 전체의 경계가 아니므로 부모가 화면 밖이어도 자식은 검사한다.
- 로더는 `expected`의 오류에 경로·행 정보를 보존한다. MTL의 기존 색상/광택 속성만 해석하며 `map_Kd`를 자동 로드하는 새 기능은 추가하지 않았다. 이미지 로딩은 `LoadImageFile`의 별도 책임이다.

### 구조 정리와 구분한 버그 수정

| 수정 | 이유와 회귀 검사 |
| --- | --- |
| 동차 클리핑 속성 보간 | 교차점의 t로 선형 보간한다. 원근 보정은 래스터화 시 한 번만 수행한다. 서로 다른 w의 UV 중간값과 6개 clip 평면을 검사한다. |
| Win32 Release 보간 경로 | 복합 vec 임시값 경로에서 입력 좌표가 훼손되는 사례가 재현됐다. 동일한 sub → mul → add 순서의 명시적 SSE 연산으로 표현하고, 입력 불변성과 같은 w의 보존을 검사한다. 허용 오차를 늘리지 않았다. |
| 클리핑 출력 초기화 | 완전히 기각된 삼각형이 직전 다각형을 재사용하지 않게 한다. |
| 선 패스 경합 제거 이력 | 초기 순차화로 타일별 전체 화면 쓰기 경합을 제거했다. 후속 와이어·큰 법선 묶음의 병렬화는 원본 순서를 보존하며 겹치지 않는 Y행만 쓴다. 작은 법선·오버레이는 순차 처리한다. |
| 디버그 선 색 | 명령의 첫 정점 색을 모든 선에 쓰지 않고 각 선의 시작 정점 색을 쓴다. 빨강/초록 두 선을 검사한다. |
| 부모·자식 연결 | 부모 링크와 world 변환을 일치시키고 부모 밖의 가시 자식이 누락되지 않게 한다. |
| 입력·메뉴 | Space 자동 반복/다른 키 입력이 회전을 반복 토글하지 않게 한다. 알고리즘 메뉴 체크 상태도 실제 선택과 일치시킨다. |
| 빈 surface | 0×0 resize에서 렌더링을 쉬고 복구 후 버퍼를 다시 만든다. |

### 검증 방법과 한계

```powershell
./tests/RunTests.ps1 -Rebuild
# 필요한 구성만 빠르게 반복
./tests/RunTests.ps1 -Platforms x64 -Configurations Debug
# x64 Release만, 이름 일부로 테스트 선택, 골든 갱신
./tests/RunTests.ps1 -Quick -Filter visibility
./tests/RunTests.ps1 -Quick -Filter golden -UpdateGolden
# Linux/클라우드: CMake 빌드 + 테스트
tools/dev.sh test
```

`RendererTests`는 외부 테스트 프레임워크 없이 실행되며 Release에서도 실패 시 종료 코드 1을 반환한다. 스크립트는 Visual Studio 설치를 찾고, 네 구성을 빌드하고, oneTBB DLL 배포와 실행 결과를 확인한다. 결과는 무시되는 `tests/bin/logs`에 남는다. 테스트는 `tests/TestMain.cpp`의 실행기가 등록 순서대로 돌리고, 테스트마다 `PASS`/`FAIL` 한 줄과 마지막 `SUMMARY`를 출력한다. 새 테스트는 해당 영역 파일의 `Register...Tests`에 한 줄 추가하고, 새 테스트 파일은 `RendererTests.vcxproj`에도 등록한다(CMake는 자동). 골든 이미지(`tests/golden/*.png`)는 headless 경로로 320×180 데모 장면을 그려 비교하며, 실패하면 `tests/out/golden/`에 actual/diff PNG를 남긴다.

행렬/법선, AABB/Frustum, 클리핑, viewport, 고정 용량, 자산 오류, worker 분리, Phong/FXAA, 키·메뉴, resize를 검사한다. `RenderTargetTests.cpp`는 메모리 stride/padding·overflow·실패 후 빈 상태·최종 행·depth/ID·FXAA와 숨은 창의 presenter 수명을 검사한다. `VisibilityTests.cpp`는 가림·구멍·같은 깊이·숨은 선·단일/다중 worker 반복 결과를 비교한다. IronMan/teapot 통합 검사는 숨겨진 Win32 창을 사용한다. Framework/GDI 테스트는 MSBuild 빌드에서만 돌고, CMake 빌드(`SR_HEADLESS_ONLY`)는 나머지 전부를 돌린다. 골든 비교는 사람의 화면 확인을 줄여 주지만 대체하지는 않는다.

FXAA의 기존 red/blue 휘도 가중치 불일치는 유지한다. BGRX native red/blue와 낮은 명도의 red/blue 경계 회귀를 고정했으므로, 이 동작을 고칠 때는 구조 변경이나 성능 실험에 섞지 말고 별도 기준 이미지를 갱신한다.

변경 전 앱의 Debug/Release × Win32/x64 빌드는 모두 성공했다. 최종 검증 결과와 남은 항목은 아래 공통 기록을 참고한다.

현재 MSVC 14.51.36231에서는 분리된 소스를 `/GL`로 링크할 때 `span` 인라인 확장 중 C1001 내부 컴파일러 오류가 발생했다. Release의 LTCG만 껐고 일반 최적화와 SSE/AVX는 유지했다. 도구체인을 갱신하면 네 구성의 테스트와 성능 측정을 거쳐 다시 활성화한다. 성능이 동일하다고 가정하면 안 된다.

## English

### Reading and changing the code

Start with `Framework::runFrame`, then `GameObject::Update`/`SubmitToRenderQueue`, and `Renderer::RenderScene(target, ...)`. Renderer coordinates preparation, binning, tiles, parallel wire/large-normal bands, sequential small-normal batches/overlays, and post-processing. `Rasterizer.cpp` replaces `RendererRasterizer.cpp`; `RenderTarget` owns depth/IDs and borrows strided pixels. `GdiPresenter` owns `GdiBackBuffer` and presentation. Memory rendering can be tested without a window, but the application/build remain Windows/MSVC, not a general headless product CLI.

`ModelLoader` separates statements, index validation/mesh assembly, missing normals, and spatial data. `AssetParsing.h` shares only token/numeric parsing with the MTL loader. MTL still handles colors/shininess, not automatic `map_Kd` loading. `SRMath.h` remains the include entry point over fixed-point/vector/matrix headers. Shared MSBuild settings and runtime sources live under `build/`.

- Use PascalCase for public member functions, camelCase for internals/locals, and `m_camelCase` for class members. Preserve math snake_case and container-compatible APIs. Apply `.clang-format` only to first-party sources.
- Document intent, coordinate systems, ownership, and concurrency in Korean. Transform column vectors with `world = parentWorld * local`; normals use the inverse transpose.
- Queue mesh/material/index references and light spans are borrowed until the synchronous render call returns. Do not call one Renderer concurrently.
- `prepareLights` snapshots each directional light once per frame into Renderer-owned `PreparedDirectionalLight` storage. It retains capacity, not old values, and uses the original `normalize(-1.f * rayDirection)` formula. The internal frame span stays read-only and valid through every worker join. Moving normalization preserves pixel values, not the timing/thread of floating-point exceptions or per-worker FP status.
- A `PixelSurfaceView` borrows top-down BGRX storage with stride in pixels; depth/IDs are separate tight arrays. Detach before DIB resize and attach only after successful creation/validation. Failure leaves an empty target; either zero axis is a valid no-op. Clear/FXAA preserve padding.
- Each Renderer owns its oneTBB worker scratch. Triangle pools use `concurrent_vector`, tile references use flat `tileReferences`, and `tileScatter` stores per-tile counts/cursors. Register `touchedTileIds` first, then increment the count only after reference insertion succeeds. `BeginFrame` clears touched metadata at its old size and flat references before the pool; viewport metadata resizes lazily. After the front-end join, the fixed active-worker list and prefix offsets assign disjoint destinations for parallel worker scatter. Scatter joins before optional sorting and rasterization of each tile span; pixel ownership and primitive IDs stay unchanged. Pure default fast X-ray skips new flat tile-reference/worker-metadata allocations and the depth prepass. No separate JobSystem is added.
- The vertex/transform cache is one entry per worker, valid only within the current frame. `BeginCachedCommand` keys on `sourceMesh` identity, `vertices.data()/size()`, the exact bits of each of the 16 world and 16 view-projection floats, and rasterize mode—not matrix-object padding or approximate values. Material and index subsets do not affect vertex-attribute computation and are excluded. Only an exact-key hit with both arrays large enough reuses the stamp, vertex results, MVP, and normal matrix. `BeginFrame` invalidates the key before other cleanup, preventing cross-frame reuse after in-place vertex edits or asset reloads. Legacy `BeginCommand(size_t)` always invalidates and advances the stamp; cache misses use that path too. Stamp wrap clears old stamps, and vertex/stamp arrays independently repair insufficient sizes. Allocation failure leaves the key invalid; publish a new key only after both storage and matrix preparation succeed. There is no global map/LRU or cross-frame cache; IDs, task distribution, and shading formulas are unchanged.
- Reuse worker-flat and final-pointer capacities instead of accumulating per-tile vector capacity. Evaluate reference-copy, prefix, and scatter costs separately. `binReferences` counts logical references once; `retainedBinBytes` includes flat/final/wire/debug storage, counts/cursors/offsets, headers, and touched/active lists. `binMs` covers front-end reference preparation; `rasterMs` includes prefix, scatter, and sorting, so distinguish its scope from earlier statistics. Speed/memory comparisons remain in progress; performance improvement and final adoption are not established.
- Prepare each clipped triangle once. Mesh command batches merge in mesh order, never worker completion order; stable source/fan IDs decide exact depth ties. Optional sorting must not change that rule.
- Tile writes are disjoint half-open ranges. Default `hiddenWireframe=false` uses fast X-ray wires; wire faces neither write depth nor occlude fill faces. Only hidden wire ON with a wire command runs the all-face depth prepass. Normals independently test existing face depth; with the option off only fill faces provide it, so pure wire scenes have no face occlusion for normals. Wires run in disjoint Y-row bands and join before normals. At least 256 DepthTested lines enable parallel preparation in 256-line chunks and original-order bins/row-band drawing; smaller batches use the sequential fallback. AABB/light overlays run sequentially after normal workers join.
- Wire bands use roughly 64 rows each, capped at 32; lines leave depth/primitive IDs unchanged. `BeginLineRows(minY, maxY)` selects a screen-clamped half-open interval, and empty bands return immediately. Bresenham does not reclip endpoints to band boundaries: `restrictBresenhamRows` reconstructs the original major-axis step's coordinates and error phase, then executes only the first through last steps belonging to the band. Tie rules and original-endpoint depth interpolation remain unchanged. Error accumulation and doubling use `int64` to avoid the former intermediate `int32` overflow at accepted extreme endpoints. DDA retains its original sequential additions. Incremental builds and runtime tests passed without warnings/errors in Debug/Release × Win32/x64, including `independentLineTraversalTests` against the original recurrence and x64 Debug ASan validation. This is not a speedup claim.
- For large DepthTested batches (at least 256 lines), resize the prepared optional array to exactly `totalLines` without clearing it first. Parallel preparation must assign this frame's valid result or `nullopt` to every slot; binning and drawing read only after the join. Small batches and empty debug frames clear the prepared array to logical size zero; commands/chunks/bins still clear every frame. A preparation exception must prevent progression to binning/drawing; the next frame recovers by overwriting every slot or clearing the array. `preparedDebugSlotFreshnessTests` checks valid/invalid slots, colors, and other stale-state risks through 769→256→257→255→0→769 transitions. Performance effects require separate measurement.
- Front-to-back and Hi-Z are independent, default-off experiments. Hi-Z uses fully covered regions' minimum inverse depth, never previous-frame/other-tile data; equal depth, holes, and uncertain bounds must pass.
- Optional `collectStats` records pass timing, prepared triangles/bin references, coverage/depth/shading, Hi-Z/line rejection, and memory. Tile-local counters merge after workers join; avoid per-pixel atomics and disable detailed counters for final timing.
- `AddChild` requires a shared-owned parent and rejects cycles, duplicates, and existing parents. Children hold weak parent links. Parent bounds do not enclose all descendants, so a culled parent must not suppress visible children.

### Behavior fixes and validation

Fixes are distinct from file/function reorganization: homogeneous clipping now interpolates attributes linearly before rasterizer perspective correction; rejected clipping clears output; an earlier sequential line pass removed cross-tile races, and later wire/large-normal parallelism preserves original order and restricts writes to disjoint rows; each debug segment uses its own color; parent transforms and child visibility work together; Space key repeat/unrelated keys no longer retoggle rotation; menu checks follow their algorithms; zero-size resize suspends rendering safely. Regression checks cover these cases.

The Win32 Release clip test also exposed input-coordinate corruption in the compound vector temporary path. Interpolation now expresses the same subtract → multiply → add sequence with explicit SSE values. Input immutability and equal-w preservation are checked without relaxing the numerical tolerance.

Run `./tests/RunTests.ps1 -Rebuild` for all four configurations, or select `-Platforms x64 -Configurations Debug`. `-Quick` builds x64 Release only, `-Filter` selects tests by name, and `-UpdateGolden` rewrites `tests/golden/*.png`; on Linux use `tools/dev.sh test`. Tests return a nonzero exit code in Release too, use no external test framework, and keep logs in ignored `tests/bin/logs`. The script discovers MSVC, builds the solution, checks oneTBB deployment, and runs the tests.

Coverage includes matrix/normal transforms, AABB/Frustum, clipping/viewport, fixed capacity, valid/invalid assets, worker state isolation, Phong, FXAA, input/menu behavior, and resize. IronMan/teapot smoke tests use hidden Win32 windows, rotation, and debug modes. Supported SIMD dispatch is compared against SSE with a numerical tolerance. These checks are not a manual visual regression or a non-AVX hardware test.

`RenderTargetTests.cpp` adds memory stride/padding, overflow, failed/zero attachment, minimal final rows, depth/IDs, FXAA, and hidden-window presenter lifecycle checks. `VisibilityTests.cpp` covers occlusion, holes, exact ties, hidden lines, and repeated single/multi-worker comparisons. FXAA's existing red/blue luminance-weight mismatch is deliberately frozen by native-pixel and low-intensity edge tests; fix it separately from structural/performance work.

All four pre-change application builds passed. See the shared validation record below for final results and outstanding checks. MSVC 14.51.36231 produced a C1001 internal compiler error in `span` inlining during `/GL` linking after the split. Release LTCG is disabled as a workaround; ordinary optimization and SIMD remain enabled. Re-enable only after toolchain retesting and performance measurements. No unchanged-performance claim is made.

## 日本語

### コードの読み方と変更時の契約

`Framework::runFrame` → `GameObject::Update` / `SubmitToRenderQueue` → `Renderer::RenderScene(target, ...)` の順に読みます。Rendererは準備・binning・tile・wire/多数のnormalの並列band・少数のnormal/overlayの逐次処理・後処理を調整します。`Rasterizer.cpp`が`RendererRasterizer.cpp`に代わり、`RenderTarget`はstride付きpixelを借用してdepth/IDを所有します。`GdiPresenter`が`GdiBackBuffer`と表示を所有します。Memory描画をwindowなしで検査できますが、アプリ/ビルドはWindows/MSVC対象で、汎用headless製品CLIではありません。

OBJ は文の解釈、インデックス検証とメッシュ構築、欠落法線生成、空間構造生成に分離しました。`AssetParsing.h` は MTL とトークン・数値解析だけを共有します。MTL は従来の色・光沢属性を扱い、`map_Kd` の自動読み込みは追加していません。`SRMath.h` は固定小数点・ベクトル・行列ヘッダーへの入口として残しています。共通ビルド設定とソース一覧は `build/` にあります。

- 公開メンバー関数は PascalCase、内部関数とローカル変数は camelCase、クラスメンバーは `m_camelCase`。数学の snake_case とコンテナー互換 API は維持します。外部ライブラリには `.clang-format` を適用しません。
- 設計意図、座標系、所有権、並列実行条件は韓国語でコメントします。列ベクトルの `world = parentWorld * local` と、法線の逆転置変換を区別します。
- キューの mesh/material/index と light span は非所有参照です。同期描画の終了まで変更・解放せず、同じ Renderer を同時に呼びません。
- `prepareLights`は毎frame、既存の`normalize(-1.f * rayDirection)`式でRenderer所有の`PreparedDirectionalLight`配列を更新します。再利用するのはcapacityだけで、同じ入力アドレスでも方向・色・個数を再取得します。内部frameのspanは全workerのjoinまで有効かつ読み取り専用です。pixel値の保存は、FP例外の発生時点・threadやworker別FP状態の同等性を意味しません。
- `PixelSurfaceView`はtop-down BGRXとpixel単位strideを借用し、depth/IDは別の詰めた配列です。DIB resize前にDetachし、生成・検証後にAttachします。失敗は空状態、片方の軸が0なら正常なno-opです。Clear/FXAAはpaddingを保持します。
- `WorkerScratch`はRendererごとのoneTBB TLSです。Triangle poolは`concurrent_vector`、tile参照はflatな`tileReferences`、tile metadataは`tileScatter`のcount/cursorで管理します。先に`touchedTileIds`を登録し、参照挿入の成功後にcountを増やします。`BeginFrame`は以前のサイズのtouched metadataとflat参照をpoolより先に消去し、viewportに合わせたmetadata resizeは必要時に行います。Front-endのjoin後、固定active worker一覧とprefix offsetで重ならない出力範囲を決め、worker別の並列scatterを実行します。Scatterのjoin後に各tile spanを任意にsortして描画し、pixel所有権とprimitive IDは維持します。標準の高速X-rayだけならflat tile参照・worker metadataの新規割り当てとdepth prepassを省きます。独自JobSystemはありません。
- 頂点・変換cacheはworker別の単一entryで、現在frame内だけ有効です。`BeginCachedCommand`のkeyは`sourceMesh`のidentity、`vertices.data()/size()`、worldとviewProjectionの各16個のfloatの正確なbit、rasterize modeです。行列objectのpaddingや近似値は比較しません。Materialとindex部分集合は頂点属性の計算に影響しないため除外します。Keyの完全一致と両配列の十分なサイズを確認したhitだけstamp・頂点結果・MVP・normal matrixを再利用します。`BeginFrame`は他の初期化より先にkeyを無効化し、同じアドレスの頂点編集やasset再読み込み後のframe間再利用を防ぎます。従来の`BeginCommand(size_t)`は必ず無効化してstampを進め、cache missも同じ経路を使います。Stamp wrapでは古いstampを消去し、頂点/stamp配列は不足サイズを個別に修復します。割り当て失敗時はkeyが無効のまま残り、両保存領域の確保と行列計算が成功した後だけ新しいkeyを公開します。全体map/LRUやframe間cacheは持たず、ID・task分配・shading式は維持します。
- Tile別vectorのcapacity累積ではなく、worker別flat参照と最終pointer配列を再利用します。参照コピー・prefix・scatterのコストは別途評価します。`binReferences`は論理参照を一度だけ数え、`retainedBinBytes`はflat/最終/wire/debug保存領域、count/cursor/offset、header、touched/active一覧を含みます。`binMs`はfront-end参照準備、`rasterMs`はprefix/scatter/sortも含むため、以前の統計と範囲を区別します。速度・memoryの比較は進行中であり、性能向上や最終採用は未確定です。
- Clip後のtriangleは一度だけ準備します。Mesh command batchはworker終了順ではなくmesh順に結合し、元triangle/fanの安定したIDで同一深度を判定します。Sortingの有無で規則を変えません。
- Tile書き込みは重ならない半開区間です。初期値`hiddenWireframe=false`は高速なX-ray wireで、wire面は深度を書かずfill面も隠しません。ONかつwire commandがある時だけ全ての面のdepth prepassを使います。Normalは独立して既存面深度を判定し、OFFではfill深度だけなので純粋なwire sceneのnormalは面に隠れません。Wireは重ならないY行bandで並列処理し、join後にnormalを処理します。DepthTested線が256本以上なら256-line chunkで並列準備し、元の順序を保つbinと行bandで描画します。少数のnormalは逐次fallbackを使い、AABB/light overlayはnormalのjoin後に逐次実行します。
- Wire bandは約64行基準で最大32個とし、線はdepth/primitive IDを変更しません。`BeginLineRows(minY, maxY)`は画面へclampした半開区間を指定し、空bandでは直ちに戻ります。Bresenhamの端点をband境界で再clipせず、`restrictBresenhamRows`が元のmajor-axis stepの座標とerror位相を復元して、bandに属する最初から最後のstepだけ実行します。Tie規則と元の端点に基づく深度補間は維持します。Errorの累積・倍算には`int64`を使い、許容範囲内の極端な端点で従来発生し得た中間`int32` overflowを避けます。DDAは元の逐次加算を維持します。元の漸化式と比較する`independentLineTraversalTests`を含め、Debug/Release × Win32/x64の差分ビルドとruntime testは警告・エラーなしで通過し、x64 Debug ASan検証も含みます。これは高速化を示すものではありません。
- 大きなDepthTested線集合（256本以上）では、準備optional配列を先に`clear()`せず、`resize(totalLines)`で論理サイズを正確に合わせます。並列準備は全slotに今回frameの有効値または`nullopt`を必ず代入し、join後だけbin構築・描画から読みます。少数の線と空のdebug frameでは準備配列を`clear()`して論理サイズを0にし、commands/chunks/binsは毎frame消去し続けます。準備中の例外ではbin構築・描画へ進まず、次frameの全slot上書きまたはclearで復旧します。`preparedDebugSlotFreshnessTests`は769→256→257→255→0→769の遷移で有効/無効slotや色などの残留を検査します。再利用の性能効果は別途測定します。
- Front-to-back/Hi-Zは独立した実験機能で初期値OFFです。Hi-Zは完全coverage領域の最小逆深度を使い、前frame/他tileは参照しません。同一深度・穴・不確実な境界は通します。
- 任意の`collectStats`でpass時間、prepared triangle/bin参照、coverage/depth/shading、Hi-Z/線棄却、memoryを記録します。Tileごとのcounterをjoin後に合算し、pixelごとのatomicを避け、最終時間比較では無効にします。
- `AddChild` は shared_ptr 管理の親だけに接続し、循環・重複・既存の親を拒否します。子の親参照は weak_ptr です。親の境界は子全体を含まないため、親が見えなくても子を判定します。

### 不具合修正と検証

構造整理とは別に、同次クリッピングでの二重の透視補正、棄却時の出力残留、タイル間の線描画競合、デバッグ線の色、親子変換と子の可視性、Space の連続トグル、アルゴリズムメニュー表示、0×0 resize を修正し、回帰検査を追加しました。線の競合は当初の逐次化で解消し、後続のwire/多数のnormalの並列化では元の順序を保ち、重ならない行だけを書き込む契約を維持します。

Win32 Release では、複合 vector 式の一時値経路で入力座標が壊れる事例も再現しました。同じ減算 → 乗算 → 加算の順序を明示的な SSE 値で表現し、入力不変性と同一 w の保存を検査しています。許容誤差は緩めていません。

`./tests/RunTests.ps1 -Rebuild` で全構成を検証します。`-Quick` は x64 Release のみ、`-Filter` は名前でテストを選び、`-UpdateGolden` は `tests/golden/*.png` を更新します。Linux では `tools/dev.sh test` を使います。`-Platforms x64 -Configurations Debug` で対象を絞れます。外部テストフレームワークは不要で、Release でも失敗時に非ゼロ終了コードを返します。スクリプトは MSVC を検出し、ビルド、oneTBB DLL 配置、テスト実行を確認します。ログは Git 対象外の `tests/bin/logs` に保存します。

行列・法線、AABB/Frustum、クリッピング・viewport、固定容量、正常・異常アセット、worker 初期化とインスタンス分離、Phong、FXAA、入力・メニュー、resize を検査します。IronMan/teapot は非表示 Win32 ウィンドウで回転・デバッグモードを実行します。利用可能な SIMD dispatch を SSE と許容誤差付きで比較します。目視回帰や AVX 非対応実機の検査とは異なります。

`RenderTargetTests.cpp`はmemory stride/padding・overflow・失敗/0サイズ・最終行・depth/ID・FXAAと非表示windowのpresenter寿命を検査します。`VisibilityTests.cpp`は遮蔽・穴・同一深度・隠線・単一/複数workerの反復比較を扱います。FXAAの既存red/blue輝度重み不一致はnative pixelと低輝度edgeのテストで維持し、構造・性能変更とは別に修正してください。

変更前のアプリは4構成ともビルド成功です。最終結果と未実施項目は以下の共通記録を参照してください。MSVC 14.51.36231 は分割後の `/GL` リンク中、`span` のインライン展開で C1001 内部コンパイラーエラーを出したため、Release の LTCG のみ無効化しました。通常の最適化と SIMD は維持しています。ツールチェーン更新後に再検証と性能測定を行ってから戻してください。性能が同一とは断定していません。

## Validation record / 검증 기록 / 検証記録

The following PASS record belongs to the earlier refactor, before buffer/visibility changes. / 다음 통과 기록은 버퍼·가시성 변경 이전 리팩터링의 결과다. / 以下の成功記録はbuffer・可視性変更前の結果です。

Current buffer/hidden-line/sort/Hi-Z validation and timings: [renderer-visibility-2026](PERFORMANCE.md#renderer-visibility-2026). No new build or performance outcome is inferred from the historical table.

- Baseline / 변경 전 / 変更前: application Rebuild passed for Debug/Release × Win32/x64.
- Historical final / 이전 변경 후 / 以前の変更後 (2026-10-05): clean solution rebuild and runtime tests passed in all four configurations; zero build warnings/errors. Logs: `tests/bin/logs/*-build.log` and `*-tests.log`.

| Configuration / 구성 / 構成 | App + tests Rebuild | Runtime tests | Warnings / 경고 / 警告 |
| --- | --- | --- | --- |
| Debug × x64 | PASS | PASS | 0 |
| Release × x64 | PASS | PASS | 0 |
| Debug × Win32 | PASS | PASS | 0 |
| Release × Win32 | PASS | PASS | 0 |

- App/test asset deployment and the architecture-specific oneTBB DLL hashes were checked for all four configurations. First-party formatting and local document links also passed validation.
- Not completed / 미실시 / 未実施: interactive camera/mouse/minimize checks, human screenshot comparison, non-AVX hardware run.
- Follow-up profiling / 후속 프로파일링 / 後続プロファイリング: [PERFORMANCE.md](PERFORMANCE.md) records VS CPU Usage, VTune, and alternating before/after frame-time measurements for fill, normals, and wireframe. These compare the post-refactor baseline with the subsequent optimizations, not the original racing line path.
- Smoke timings printed by tests include eight mixed-mode frames at 160×120; they are diagnostics, not a before/after performance benchmark.
- oneTBB/stb_image versions, SSE/AVX dispatch, C++26 Preview, and the existing module pilot are retained. No new Job System, headless runtime, full module migration, commit, or push is part of this change.

## Benchmark commands / 벤치마크 명령 / ベンチマークコマンド

Run from the repository root after a Release x64 build. / Release x64 빌드 후 저장소 루트에서 실행한다. / Release x64ビルド後、リポジトリルートで実行します。

```powershell
# IronMan/teapot: hidden Framework/GDI path
.\tests\bin\x64\Release\RendererTests.exe . --benchmark 600 1280 720 fill none
.\tests\bin\x64\Release\RendererTests.exe . --benchmark 600 1280 720 normal both stats
.\tests\bin\x64\Release\RendererTests.exe . --benchmark 600 1280 720 wire none nostats hidden

# Synthetic memory target: heavy occlusion or mostly separate objects
.\tests\bin\x64\Release\RendererTests.exe . --visibility-benchmark 600 1280 720 fill both occluded
.\tests\bin\x64\Release\RendererTests.exe . --visibility-benchmark 600 1920 1080 fill none open
.\tests\bin\x64\Release\RendererTests.exe . --visibility-benchmark 120 1280 720 wire none occluded nostats hidden
```

- Modes / 모드 / モード: `fill | normal | wire | fxaa`; Framework/GDI additionally accepts `aabb`.
- Options / 옵션 / オプション: `none | sort | hiz | both`; visibility scenarios / 장면 / シーン: `occluded | open`.
- The diagnostics slot accepts `stats`; use `nostats` when appending the final `hidden` token without instrumentation. No `hidden` token means default X-ray wire. / 진단 자리는 `stats`이며, 계측 없이 마지막 `hidden`을 붙일 때는 `nostats` 자리를 명시한다. `hidden`이 없으면 기본 X-ray 와이어다. / 診断位置は`stats`です。計測なしで末尾の`hidden`を付ける場合は`nostats`を明記します。`hidden`なしは標準X-ray wireです。
- Both paths warm up 60 frames. Compare mean/p95 and pixel hashes with identical behavior; keep diagnostics off for final timing. Synthetic results exclude presentation and scene update. / 두 경로는 60프레임 준비 후 같은 동작의 평균·p95·픽셀 해시를 비교한다. 합성 장면은 화면 표시와 장면 갱신 비용을 포함하지 않는다. / 両経路は60フレーム準備後に同一動作の平均・p95・pixel hashを比較し、最終計測では詳細診断を切ります。合成シーンは表示・scene更新を含みません。
