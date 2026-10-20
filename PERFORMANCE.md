# Renderer performance / 렌더러 성능 / レンダラー性能

## VS and VTune resumed measurements (2026-10-07)

**한국어:** Intel `EnduranceGamingProcess`를 사용자 승인으로 종료한 뒤, 보존한 세 실행 파일을
기준 버전 → 절두체만 변경한 버전 → 광원 준비까지 변경한 버전 순서와 그 역순으로 측정했습니다.
VS CPU Usage 18회와 VTune software Hotspots 18회가 정상 종료했고, VTune 18회의 마지막 프레임
픽셀 해시는 모드별로 모두 일치했습니다. 절두체 변경은 두 차례 모두 전체 시간이 낮게 관측됐으나,
광원 변경은 일관된 개선을 보이지 않았습니다. **아래 수치는 검증된 A/B 결과가 아닌 원시 관측값**입니다.
빌드 시작 이벤트 구독 실패로 모든 수집의 `valid_for_comparison=false`를 유지했고, 버전·모드당
반복은 두 번뿐입니다. 수집 완료를 전체 최적화 목표 달성으로 간주하지 않습니다.

**English:** After terminating Intel's `EnduranceGamingProcess` with user approval, the preserved
baseline, Frustum-only and prepared-light executables were profiled in forward and reverse order.
All 18 VS CPU Usage and 18 VTune software Hotspots captures exited successfully; the 18 VTune
final-frame pixel hashes matched by mode. Frustum elapsed time was lower in both rounds, while
prepared lights did not consistently improve it. **These are raw observations, not a verified A/B
comparison:** build-start event subscription failed, every capture retains `valid_for_comparison=false`,
and there are only two observations per version/mode. Capture completion is not optimization-goal completion.

**日本語:** ユーザーの許可でIntelの`EnduranceGamingProcess`を終了し、保存済みの基準版、
Frustum変更版、光源準備変更版の順と逆順で計測しました。VS CPU Usage 18回とVTune software
Hotspots 18回は正常終了し、VTune 18回の最終frameのpixel hashはmodeごとに一致しました。
Frustum変更の全体時間は両方の計測で低く観測されましたが、光源変更は一貫した改善を示しません。
**以下は未検証の観測値であり、検証済みA/B比較ではありません**。build開始イベントの購読に
失敗したため全captureの`valid_for_comparison=false`を維持し、各版・modeの反復は2回だけです。
収集完了を最適化目標の達成とはみなしません。

### Workload and evidence

- Release x64, IronMan + teapot, fixed camera, 1280 × 720, 60 warm-up frames followed by 1,200 frames.
  Fill, normal and default fast X-ray wire; FXAA, hidden-wire removal, sorting, Hi-Z and counters OFF.
  VS and VTune ran sequentially, never concurrently. No build was launched by this measurement task.
- Capture windows on October 7 (KST), round 1: baseline 19:25:18–19:27:55;
  Frustum-only 19:29:47–19:32:00; prepared lights 19:33:28–19:35:50.
  Round 2: prepared lights 19:42:30–19:44:47; Frustum-only 19:45:32–19:47:53;
  baseline 19:48:40–19:50:58. Each version still uses fill → normal → wire order within its round.
  These are new captures of the old binaries, not a timing comparison across October 5 and October 7.
- VTune **Elapsed** covers the profiled process, including startup, asset loading, warm-up and shutdown;
  it is not render-only time or FPS. **Effective CPU** is the profiler's summed effective CPU time across
  threads, not wall time. Application `BENCH` chrono/FPS fields and emitted-code byte counts are excluded.
- VS uses exclusive instruction-pointer samples after the first 5,000 ms from ETL start, filtered to the
  exact captured EXE image. The busiest-thread percentage below excludes TBB/system DLL samples and is
  **not** whole-process CPU utilization, inclusive function time or proof of equal worker completion.
  All 18 VS reports matched their PDB exactly and observed 24 known sampled thread IDs in that scope.
- Original EXE hashes match the three prescribed snapshots. Rechecking all copied EXE/PDB/DLL/asset files
  against their manifests passed; the TBB DLL and four asset hashes agree across all six capture suites.
  All 36 target/launcher exit codes are zero. Pixel hashes were checked in VTune target stdout only;
  the VS collector log does not independently provide them. A matching final hash is not a comparison
  of every intermediate frame or a general data-race proof.

Each numeric cell lists **round 1 / round 2**, not a mean or confidence interval.
각 수치는 **정순 / 역순** 관측값입니다. 各数値は**順方向 / 逆方向**の観測値です。

| Version | Mode | VTune elapsed (s) | VTune effective CPU (s) | VS busiest thread share of EXE self samples (%) |
|---|---|---:|---:|---:|
| Baseline (8) | Fill | 9.751 / 8.964 | 43.896 / 42.964 | 5.984 / 5.290 |
| Frustum-only (9) | Fill | 8.288 / 7.880 | 43.437 / 41.256 | 4.912 / 5.597 |
| Prepared lights (10) | Fill | 12.168 / 8.513 | 40.384 / 43.483 | 5.000 / 5.188 |
| Baseline (8) | Normal | 16.488 / 16.644 | 45.663 / 45.704 | 7.715 / 7.998 |
| Frustum-only (9) | Normal | 14.938 / 15.086 | 43.999 / 42.829 | 7.909 / 7.749 |
| Prepared lights (10) | Normal | 16.060 / 14.843 | 46.073 / 42.169 | 8.022 / 8.212 |
| Baseline (8) | Wire | 11.282 / 10.802 | 21.739 / 22.393 | 5.353 / 5.668 |
| Frustum-only (9) | Wire | 9.747 / 10.201 | 20.354 / 20.736 | 5.977 / 6.284 |
| Prepared lights (10) | Wire | 10.237 / 9.410 | 19.524 / 19.537 | 5.917 / 6.546 |

**한국어 — 두 변경의 해석:** 절두체만 변경한 8→9의 전체 시간 감소는 fill/normal/wire 순으로
1차 15.00%/9.40%/13.61%, 2차 12.09%/9.36%/5.56%였습니다. 유효 CPU 시간도 여섯 비교에서
모두 낮았습니다. VS의 `IsAABBInFrustum` self sample은 1차에 2,162→672, 2,904→686,
3,640→507, 2차에 2,271→510, 2,716→569, 2,216→512로 줄었습니다. 개선 방향은 두 번 반복됐지만
외부 부하 배제가 검증되지 않은 두 관측만으로 확정 개선율이나 통계적 유의성을 주장하지 않습니다.

광원 변경만 분리한 9→10의 fill 전체 시간은 1차 46.81%, 2차 8.03% 늘었습니다. fill 유효 CPU
변화도 -7.03%에서 +5.40%로 뒤집혔고, normal/wire 전체 시간은 1차에는 늘고 2차에는 줄었습니다.
같은 광원 변경판의 fill 전체 시간 자체가 12.168초와 8.513초로 달랐습니다. 따라서 광원 변경을
검증된 전체 처리량 개선으로 분류하지 않습니다. VS sample 감소율은 속도 향상률이 아니며,
worker의 최대 점유율도 일관되게 낮아지지 않았습니다. 점유율을 맞추기 위해 불필요한 작업을 추가하지 않습니다.

**English — independent comparisons:** For 8→9, elapsed time was lower by 15.00%/9.40%/13.61% in
fill/normal/wire in round 1 and 12.09%/9.36%/5.56% in round 2. Effective CPU was also lower in all six
comparisons. VS `IsAABBInFrustum` self samples fell from 2,162→672, 2,904→686 and 3,640→507 in round 1,
and 2,271→510, 2,716→569 and 2,216→512 in round 2. The direction repeated, but two observations without
verified workload isolation do not establish a confirmed improvement percentage or statistical significance.

For the separate 9→10 light change, fill elapsed time rose by 46.81% and 8.03%; its effective CPU change
reversed from -7.03% to +5.40%. Normal/wire elapsed increased in round 1 but decreased in round 2.
The same prepared-light executable's fill elapsed itself varied from 12.168s to 8.513s. Prepared lights
therefore remain **unproven as a throughput optimization**. Function sample reductions across different
capture windows are not speedup percentages, and busiest-thread share did not consistently improve.
Artificial work to equalize percentages is not an optimization. No overall worker-balance improvement is signed off.

**日本語 — 変更を分けた比較:** 8→9の全体時間の減少はfill/normal/wireの順で、
1回目15.00%/9.40%/13.61%、2回目12.09%/9.36%/5.56%でした。effective CPU時間も6比較すべてで
低く、VSの`IsAABBInFrustum` self sampleも両方の計測で減りました。方向は再現されましたが、
外部負荷の排除が未検証の2観測だけで確定した改善率や統計的有意性は主張しません。

光源変更だけの9→10ではfill全体時間が46.81%と8.03%増え、effective CPUの変化は
-7.03%から+5.40%へ反転しました。normal/wire全体時間も1回目は増加、2回目は減少しました。
同じ光源変更版のfill全体時間が12.168秒と8.513秒に変動しており、全体の高速化は未検証です。
sample数の減少を高速化率に換算せず、workerの最大占有率も一貫して改善していないと記録します。
占有率を揃えるための不要な処理は追加しません。

| Final-frame pixel hash | All three VTune versions in both rounds |
|---|---|
| Fill | `4360798350140288590` |
| Normal | `16630578814480127575` |
| Wire | `504747146023326269` |

### Reproduction and remaining limits

Local evidence directories are
[`resume-r1-candidate8-raw`](tests/bin/profiles/goal-opt-20261007/resume-r1-candidate8-raw/),
[`resume-r1-candidate9-raw`](tests/bin/profiles/goal-opt-20261007/resume-r1-candidate9-raw/) and
[`resume-r1-candidate10-raw`](tests/bin/profiles/goal-opt-20261007/resume-r1-candidate10-raw/), followed by
[`resume-r2-candidate10-raw`](tests/bin/profiles/goal-opt-20261007/resume-r2-candidate10-raw/),
[`resume-r2-candidate9-raw`](tests/bin/profiles/goal-opt-20261007/resume-r2-candidate9-raw/) and
[`resume-r2-candidate8-raw`](tests/bin/profiles/goal-opt-20261007/resume-r2-candidate8-raw/).
Each retains `manifest.json`, `captures.json`, exact binary/assets, VS `.diagsession`/ETL and
`vs-<mode>-etl0-warmup5000-analysis.stdout.txt`, plus VTune raw results and
`vtune-<mode>-summary-report.stdout.txt`, hotspots and thread reports. These large ignored artifacts
are local evidence, not files promised to exist in a fresh clone. No capture or failure log was deleted.
Extracted metrics, separate per-round deltas, definitions and hash/exit/PDB checks are retained in
[`resume-comparison.json`](tests/bin/profiles/goal-opt-20261007/resume-comparison.json).
Relative changes use `100 * (after / before - 1)` within each round; negative means lower, not necessarily
a verified speedup. Original profiler reports remain the controlling evidence.

The existing `tests/CaptureCpuProfiles.ps1` was run with `-Modes fill,normal,wire -Frames 1200 -Analyze`
and `-AllowUnverifiedBuildMonitor`, using a new output directory and the respective preserved binary
for each version. The strict attempt in `resume-strict-candidate8` stopped before profiler launch.
The raw override permits collection, **not verification**: WMI subscription reported `AccessDenied`
in 27 captures and `Failed` in nine. Empty build-event lists under that failure do not prove an idle machine.

After the last capture, a read-only process check found `multitask_app_tests` PID 17768, created at
19:36:41 KST; subsequent checks also saw other application tests. Reusable MSBuild children were
observed with creation times 19:35:51–19:35:52. These observations do not establish the start or absence
of every earlier build operation. The first repeat attempt stopped before creating a `resume-r2-*`
directory. After the test runner and its observed tests exited, reverse-order 10→9→8 collection resumed
and completed in new directories. No unrelated process was terminated. Three explicitly reusable
MSBuild workers remained at the start of each round-2 suite; their warnings are preserved. Their presence
and occasional process snapshots do not certify the absence of a short intervening build or other load.

The requested two-direction collection is complete; **verified speedup and worker-balance conclusions
remain outstanding**. A functioning build-event monitor and controlled workload isolation are needed for
that sign-off. Do not keep collecting identical unverified runs as if count alone fixed the missing evidence.
The waiting automation remains paused. Product code, priority/power settings, commits and pushes were
not changed during this measurement-only follow-up.

Intel's process was stopped, but removing its machine-wide startup entry was denied by registry
permissions. The entry was therefore **not permanently disabled**, and its restore backup was preserved.
No driver, process priority, power plan or OS security setting was changed. The observed Windows power
scheme was Balanced; the user's OEM quiet-mode setting was not independently verified. New measurements
must preserve the current environment instead of treating earlier power-mode results as controlled peers.

## Assembly guided Frustum optimization

2026-10-07 — the optimization goal resumed. The previous closing report remains a dated snapshot.

**한국어:** 이전 VS/VTune의 절두체 검사 hotspot을 바탕으로 Release x64 `Frustum.obj`를 확인했습니다.
`IsAABBInFrustum`의 P-vertex 선택에는 평면마다 xyz 조건 분기 세 개와 임시 정점의 stack 저장 및
재읽기가 남아 있었습니다. 선택을 ordered SIMD 비교와 `blendvps`로 바꾸고, 기존 `dpps 0x71`,
거리 덧셈, 평면 순서와 조기 반환은 유지했습니다. NaN은 min, +0/-0은 max를 선택합니다. 미사용
네 번째 normal lane은 비교 전에 비산술 `blendps`로 지워 불필요한 MXCSR invalid flag를 방지합니다.

**English:** The Release x64 P-vertex loop now uses an ordered packed comparison and `blendvps` instead
of three scalar axis branches and temporary stack writes. The original dot mask, addition order and
early-out order remain. NaN selects min and either signed zero selects max. Clearing the unused normal
lane before comparison preserves its exclusion from floating-point exception status. The ISA requirement
does not exceed the existing SSE4.1 dot path; SSE/AVX dispatch and global floating-point options are unchanged.

**日本語:** Release x64のP-vertex選択を、3軸の分岐とstack更新からordered SIMD比較と`blendvps`へ
変更しました。内積mask・加算順序・平面順序・早期returnは維持します。NaNはmin、±0はmaxを選び、
未使用のnormal第4laneは比較前に消去して不要なMXCSR例外flagを防ぎます。既存のSSE4.1要件や
SSE/AVX dispatch、浮動小数点optionは変更していません。

| Release x64 object-code property | Before | After |
|---|---|---|
| Per-plane axis-selection conditional jumps | 3 | 0 |
| Repeated P-vertex stack writes and reload | Present | Absent |
| Emitted plane-loop byte range | `[0x20, 0x7E)` = 94 bytes | `[0x21, 0x55)` = 52 bytes |
| Function-level preservation cost | Stack temporary | One nonvolatile XMM register save/restore |

These are emitted-code observations, **not cycle counts or speedup percentages**. An intermediate
AND/ANDNOT/OR version required two nonvolatile XMM saves; the retained `blendvps` version requires one.
MSVC v145 14.51.36231 produced both objects. Before/intermediate/after disassembly is retained locally under
`tests/bin/profiles/goal-opt-20261007/before9-source/` and `candidate9-source/`. Object SHA256 values are
`82252AB2F02D5470C5E9D8D8CE8493D1B0D7D5B72125DBF059FF6CDE0C6FE435` before and
`B448630DCF1D8AB549D1993E695D8B1E13654AD80A46717773502CF886A8E1F8` after.

All four incremental builds/runtime suites passed with zero warnings/errors, including x64 Debug ASan.
`frustumSelectionTests` independently preserves the original scalar-if selection and dot recurrence.
It checks 2,048 seeded boxes with unnormalized planes, each plane's one-ULP boundary, signed zero,
NaN/infinity/subnormal/extreme inputs and dirty fourth lanes. Quiet/signaling NaN fourth-lane witnesses
also check MXCSR invalid status while restoring the original control/status register before assertions.
Release x64 test-binary SHA256: `7A92F80FFA4585C7E184D48F22C6FE9660E8D3DB481F44653AEA5B0884D17CE1`.

The October 7 forward/reverse VS/VTune observations are recorded in
[resumed measurements](#vs-and-vtune-resumed-measurements-2026-10-07).
Verified throughput and worker-balance validation remain **pending**; the smaller loop alone does not
establish higher FPS. 한국어: 정순·역순 원시 수집은 완료했지만 처리량·worker 분포 개선은 아직 미검증입니다.
日本語: 順方向・逆方向のraw収集は完了しましたが、全体性能とworker分布の改善は未検証です。

## Frame prepared directional lights

2026-10-07 — a separate shading candidate preserves the Frustum-only executable for independent comparisons.

**한국어:** 기존 VTune fill 캡처에서 `ShadeSurface`와 `powf`가 비용 상위에 있었고, Release x64
기계어에는 모든 픽셀의 광원 루프에서 같은 방향의 `sqrtss`와 `divss`가 반복되고 있었습니다.
`PreparedDirectionalLight`에 `normalize(-1.f * rayDirection)`과 색을 프레임당 한 번 준비합니다.
Renderer가 저장소를 소유하고 용량만 재사용하며, worker 시작 전 완성한 값을 join까지 읽기만 합니다.
광원 입력 주소가 같아도 매 프레임 값과 개수를 갱신합니다. 반사·`pow`·색 누적 순서는 바꾸지 않습니다.

**English:** Directional-light normalization moved from the per-pixel light loop into frame preparation.
Renderer-owned storage retains capacity, refreshes every light in submission order each frame, and stays
read-only until workers join. The original normalization formula, tiny-vector behavior, reflection, power
function and color accumulation order remain unchanged. The public `RenderScene` input is unchanged;
the internal frame span contains prepared values instead of raw lights.

**日本語:** 各pixelで繰り返していた光源方向の正規化をframe準備へ移しました。
Renderer所有の配列はcapacityだけを再利用し、光源の数・方向・色を毎frame更新します。
worker開始前に準備を完了し、joinまでは読み取り専用です。正規化式、微小vectorの扱い、
反射・`pow`・色の加算順序と公開`RenderScene`入力は維持します。

| Release x64 emitted-code property | Frustum-only build | Prepared-light build |
|---|---|---|
| Light normalization in the pixel loop | `sqrtss`/`sqrtf` fallback and `divss` | Moved to frame preparation |
| Light-loop byte range | `[0x1C0, 0x355)` = 405 bytes | `[0x1C0, 0x2CD)` = 269 bytes |
| Camera-to-surface normalization | Per pixel | Still per pixel |
| Phong specular power | `powf` | `powf` unchanged |

These are object-code properties, not measured cycle counts or a speedup. The before/after
`RendererShading.obj` SHA256 values are `7FC8F0941EBC2EB5E0776339C1988F92F24D82C678814B4CF20FF4C6703A8551`
and `E00E4C4BAB1FF7C561544D130CA8402C8B93928D1BC191FB683B69B3979852C5`.
Objects and disassembly are under `tests/bin/profiles/goal-opt-20261007/candidate9-preserved/source/`
and `candidate10-source/` respectively.

All four incremental builds/runtime suites passed with zero warnings/errors, including x64 Debug ASan.
The independent old per-pixel Phong oracle covers 2,016 color
comparisons across zero/tiny/unnormalized/NaN/infinite directions, textures, materials, cameras and multiple
light orders. Finite values are compared bitwise and NaNs by classification. Additional tests cover
same-address mutation, N→0→N light counts, retained storage, two Renderer instances and fill/wire/empty
frame transitions. This does **not** preserve the entire floating-point environment: eager preparation
moves normalization exceptions/status changes to the caller, including frames with no shaded pixels.
Worker-specific exception flags and unmasked trap timing/thread equivalence are not claimed.
한국어: 픽셀 값 보존과 전체 FP 예외 환경의 동등성은 별개입니다. 日本語: pixel値とFP例外環境全体の同等性は別です。

Forward/reverse VS/VTune collection is complete; see
[resumed measurements](#vs-and-vtune-resumed-measurements-2026-10-07) for the mixed observations and limits.
Verified comparisons remain pending. Keep the two changes separate: the preserved Oct 5 baseline →
Frustum-only build, then Frustum-only → prepared-light build.
The Frustum-only EXE/PDB/DLL/assets are preserved at
`tests/bin/profiles/goal-opt-20261007/candidate9-preserved/snapshot/` with EXE SHA256
`7A92F80FFA4585C7E184D48F22C6FE9660E8D3DB481F44653AEA5B0884D17CE1`.
The prepared-light EXE/PDB/DLL/assets are separately preserved at
`tests/bin/profiles/goal-opt-20261007/candidate10-preserved/snapshot/`. Its Release x64 EXE SHA256 is
`9F12B538900D4EAB66BC7D97AE3DB32E0C8883B4E2669E4620E1823F4AD1B1CE`.
No chrono/FPS output from correctness tests is used as performance evidence.

<a id="profiler-optimization-2026"></a>
## Profiler-guided optimization record (2026-10-05)

작업 종료 요약은 [최적화 결과 보고서](OPTIMIZATION_REPORT.md)에 있습니다. 사용자 요청으로 추가
실험을 중단했으며, 아래는 측정 이력입니다. 전체 최대 성능 달성을 뜻하지 않습니다.
Further experiments were stopped at the user's request; the following is the measurement record, not a
claim of maximum performance. ユーザーの依頼で追加実験を終了しました。以下は測定履歴です。

### 한국어

이번 최적화의 성능 판단 근거는 **VS CPU Usage와 VTune software Hotspots만**입니다.
실행 파일이 출력하는 `BENCH`의 자체 chrono 시간은 판단에 쓰지 않으며, 픽셀 해시는 정확성 확인용입니다.
아래 예전 절의 자체 시간 측정과 이번 프로파일 결과는 섞어 비교하지 않습니다.
목표는 worker의 작업 편중과 전체 소요 시간을 함께 줄이는 것입니다. 함수 점유율을 같게 만들거나,
점유율을 맞추려고 불필요한 일을 추가하는 것은 목표가 아닙니다.

VS의 기본 와이어 캡처에서 정점/삼각형 준비와 순차 선 루프를 확인한 뒤 다음을 변경했습니다.

- 와이어에서 사용하지 않는 world/normal/UV 변환과 역전치 행렬 계산을 생략합니다.
- clip 내부 삼각형은 임시 버퍼 복사 없이 원본 정점 캐시를 읽습니다. 3항 min/max는 고정 비교입니다.
- 와이어를 서로 겹치지 않는 Y행 구간으로 나눕니다. 원래 끝점부터 선을 진행하고 쓰기 행만 제한하므로
  Bresenham/DDA의 오차 누적과 경계 픽셀을 유지합니다. 깊이/owner는 읽기 전용입니다.
- 첫 병렬화에서 VS/VTune이 전체 삼각형 풀의 반복 검색을 새 hotspot으로 보여 주었습니다.
  두 번째 수정은 worker별 재사용 `wireBins`에 겹치는 삼각형만 등록해 이 검색을 제거합니다.
  후속 법선 병렬화와 그 비용은 아래 `candidate3` 절에서 별도로 기록합니다. 오버레이는 순차 패스입니다.

네 구성(Debug/Release × x64/Win32)의 증분 빌드·테스트가 통과했고 경고/오류는 0개입니다.
x64 Debug는 ASan을 사용했습니다. 행 경계, 두 선 알고리즘, 숨은 선 ON/OFF, stride padding,
깊이·owner·기각 통계, 단일/다중 worker와 여러 band를 가로지르는 100회 반복 검사를 포함합니다.
실제 표시 창의 수동 검증, 최종 버전의 1080p/4K 재프로파일, 반복 캡처에 의한 변동성 검증은 남아 있습니다.
따라서 **최대 성능 달성 또는 전체 모드 최적화 완료를 주장하지 않습니다.**

### English

Only VS CPU Usage and VTune software Hotspots determine performance in this iteration; application chrono
output is not evidence, and hashes only check correctness. Do not mix these profiles with historical benchmark
times below. Unused wire attributes, accepted-triangle copies, and generic three-element min/max work were
removed. Disjoint row bands distribute wire writes without changing line stepping or depth ownership.
The first band implementation exposed repeated pool scans; worker-private reusable band lists remove them.
The subsequent normal-line change is recorded separately under `candidate3` below; overlays remain sequential.
All four incremental builds/tests for the private-wire-list version passed, including x64 Debug ASan and
multi-band pixel/depth/owner/statistics equivalence. Maximum performance is **not** established: fill
rasterization/binning, repeated captures, final 1080p/4K profiles, and interactive validation remain.

### 日本語

今回の性能判定はVS CPU UsageとVTune software Hotspotsだけに基づきます。アプリのchrono出力は
性能根拠にせず、hashは正しさの確認にのみ使います。下記の過去の測定とは混ぜません。
wireの不要属性変換、clip内三角形のコピー、汎用3要素min/maxを削減し、重ならないY行bandへ分散しました。
端点からの線の進行とdepth/ownerを維持します。最初のband実装で全poolの反復検索がhotspotとなったため、
worker専用の再利用band listに変更しました。後続の法線並列化は下記candidate3節に分けて記録します。
overlayは逐次のままです。
4構成の差分build/testとx64 Debug ASan、多bandのpixel/depth/owner/統計比較は成功しました。
ただし**最大性能・全modeの最適化完了ではありません**。fill rasterization/binning、
反復capture、最終1080p/4K profile、実画面操作の検証は残っています。

### First captures, not a statistical speedup claim

Same workload: Release x64, 1280×720, rotating IronMan/teapot, 60 warmup + 1,200 measured frames,
default X-ray policy, sorting/Hi-Z/statistics OFF. Profilers run separately and without builds/tests.
VTune elapsed includes startup, warmup, render/update/presentation and shutdown; it is **not frame time**.
VS shares below use all captured EXE self instruction-pointer samples (`WarmupMs=0`), excluding DLL samples;
they are **not whole-machine CPU utilization or inclusive call-tree percentages**. Twenty-four TIDs were sampled.
Power/temperature/background activity were not controlled. Each table cell is one capture, not a median.

| Mode | VTune elapsed: before → band scan → private band lists | VTune effective CPU: before → band scan → private lists | VS busiest EXE thread share: before → band scan → private lists |
|---|---:|---:|---:|
| Wire | 18.009 → 13.816 → 14.652 s | 22.716 → 23.291 → 15.055 s | 23.246 → 7.903 → 6.817% |
| Normals | 22.625 → 22.857 → 21.737 s | 38.651 → 34.219 → 34.595 s | 15.995 → 16.689 → 16.711% |
| Fill | 11.952 → 10.258 → 9.743 s | 49.245 → 43.507 → 50.541 s | 5.581 → 5.965 → 5.510% |

Wire distribution improved, but fewer samples in one function do not by themselves prove a speedup.
The private-list capture used less effective CPU than the scan version without improving its single-run elapsed
time. Normal distribution remains uneven; fill effective CPU and elapsed moved in opposite directions.
These observations require repeated paired captures, not a blanket improvement claim. VTune wait/spin labels
alone do not establish lock contention. Some system/vendor symbols remain unavailable; own EXE/PDBs resolve.
VS lacks actual ProcessStart in these traces, so the analyzer reports process elapsed as `missing`, not an estimate.

Final VTune workload hashes match the initial baseline: wire `504747146023326269`, normals
`16630578814480127575`, fill `4360798350140288590`. These are last-frame hashes, not full-sequence comparisons.

Local ignored evidence under `tests/bin/profiles/goal-opt-20261005/`: `vs-before-*`, `vtune-before-*`,
`vs-after-*`/`vtune-after-*` (first band-scan candidate), and `candidate2/` (private-wire-list candidate).
The initial `before/` and first `after/` folders preserve EXE/PDB; `candidate2/manifest.json` records the
self-contained EXE/PDB/DLL/assets snapshot hashes. Its `captures.json` records the launched PIDs and exit codes.

### Candidate3 — ordered parallel normal lines / 법선 병렬화 / 法線並列化

**한국어:** 기존 빠른 X-ray 와이어를 기본으로 유지하고, 숨은 선 제거는 선택 옵션입니다.
DepthTested 선이 256개 이상이면 256선 chunk로 변환·클리핑을 병렬 준비한 뒤, 원래 명령/정점 순서대로
Y행 band에 등록합니다. band별 색상 쓰기는 겹치지 않으며 깊이/owner는 읽기만 합니다. 256개 미만은
기존 순차 경로, 오버레이는 모든 선 작업이 끝난 뒤 순차 실행합니다. 색이 다른 교차선의 덮어쓰기 순서를
보존하기 위해 bin 구성은 직렬입니다. 준비 배열·명령 metadata 용량을 재사용하며 통계의
`liveDebugLineBytes`/`retainedDebugLineBytes`로 기록합니다. 이 추가 메모리와 작업 분배는 무료가 아닙니다.

**English:** Fast X-ray wire remains the default; hidden-line removal is optional. At least 256 depth-tested
lines enable parallel transform/clip preparation in 256-line chunks, followed by stable submission-order Y-band
lists. Bands write disjoint rows and only read depth/owners. Smaller batches retain the sequential path;
overlays run sequentially after the join. Serial bin construction preserves colored crossing-line order.
Reusable prepared-line/command storage is reported separately as live/retained debug-line bytes.

**日本語:** 高速X-ray wireが既定で、隠線除去は選択式です。DepthTested線が256本以上なら256本chunkで
変換・clipを並列準備し、元の提出順を維持したY行bandへ登録します。色の書き込み行は重ならず、
depth/ownerは読み取り専用です。256本未満とjoin後のoverlayは逐次です。交差線の色順序を守るため
bin構築は逐次処理です。再利用する準備配列・命令metadataのlive/retainedメモリも統計に含めます。

Fresh paired workload: normal mode, same 1,200 frames/1280×720/options as above. One capture per version:

| Metric | Fresh sequential-normal baseline | Candidate3 |
|---|---:|---:|
| VTune elapsed, whole process | 20.901 s | 19.265 s |
| VTune effective CPU time | 32.179 s | 42.765 s |
| VS busiest EXE self-sample thread share | 16.806% | 9.515% |
| VS total matched EXE self samples | 85,848 | 96,216 |

**해석 / Interpretation / 解釈:** Elapsed fell in this capture and the most-loaded EXE thread's share fell,
but effective CPU increased. This is not proof of a repeatable speedup or better CPU efficiency. Profiler
overhead, scheduling, power and background variability remain uncontrolled. Both VTune runs ended with hash
`16630578814480127575`; only the last frame is compared here. Evidence is preserved in
`normal-before3/` and `candidate3/` under the local ignored profile directory, each with matching binary/PDB,
asset manifest and successful VS/VTune exit records. Candidate3 tests additionally compare the old independent
sequential line implementation with parallel pixels/depth/owners/statistics, mixed line colors, 255/256-line
thresholds, multiple preparation chunks, odd tails, rejected coordinates, empty debug queues and resize.

Candidate3 validation after strengthening the chunk-sentinel and threshold checks: all four incremental
Debug/Release × x64/Win32 builds and runtime suites passed with zero build warnings/errors; x64 Debug uses
ASan. This is not a clean rebuild or interactive visual sign-off. Repeated paired captures and final
1080p/4K profiling remain outstanding.
한국어: 경계 테스트 보강 후 네 구성 증분 빌드·테스트가 경고/오류 없이 통과했습니다. clean rebuild,
실제 창의 수동 검증, 반복 및 최종 고해상도 프로파일은 아직 완료하지 않았습니다.
日本語: 境界テスト強化後、4構成の差分build/testは警告・エラーなしで成功しました。clean rebuild、
実画面の手動検証、反復・最終高解像度profileは未完了です。

The remaining fill hotspot is supported by both candidate2 reports: VS `binTriangle` accounts for
13,648 of 75,803 EXE self samples; VTune tile-pointer `concurrent_vector::emplace_back` takes 4.289 self CPU s.
That evidence motivated the worker-private tile-bin experiment below. Wait/spin labels alone do not
prove contention.

### Candidate4 — private tile bins / worker별 타일 목록 / worker専用tile bin

**한국어:** frontend가 공유 `concurrent_vector<TriangleRef*>`에 삽입하던 경로를 worker별 일반 벡터로
바꿨습니다. 실제 사용한 타일만 프레임 경계에서 비우고, frontend join 뒤 활성 worker 목록을 고정합니다.
각 타일 담당자가 자기 최종 벡터에 병합한 다음 기존 정렬·Hi-Z·깊이/색상 패스를 실행합니다. 삼각형 풀의
주소 안정성을 위한 `concurrent_vector`와 primitive ID 규칙은 유지합니다. X-ray wire는 private tile
배열도 새로 만들지 않습니다. 병합 복사와 worker×tile 헤더 메모리는 새 비용이므로 성능 채택 판단에
포함해야 합니다. 타일 처리 순서가 달라질 수 있어 shaded/depth-rejected/Hi-Z 작업량 카운터 자체는
동일성 조건이 아니며, 최종 픽셀·깊이·owner가 동일해야 합니다.

**English:** Front-end pointer insertion now targets worker-private vectors. Only touched bins are cleared;
after the front-end join, active worker references are frozen. Each tile task merges into its own final vector
before the unchanged sorting, Hi-Z and depth/color passes. The address-stable concurrent triangle pool and
primitive IDs remain unchanged. Pure X-ray wire does not allocate private tile arrays. Merge copying and
worker×tile vector headers are additional costs. Order-sensitive work counters may differ; final
pixels/depth/owners must not.

**日本語:** frontendの共有pointer挿入をworker専用vectorへ変更しました。使用したbinだけをframe境界で
clearし、join後にactive worker参照を固定します。各tile担当が自分の最終vectorへ併合してから従来の
sort・Hi-Z・depth/color passを実行します。アドレス安定性のある三角形poolとprimitive IDは維持し、
純粋なX-ray wireにはprivate tile配列を新規確保しません。併合コピーとworker×tileのvector headerは
追加コストです。順序に依存する処理回数は変わり得ますが、最終pixel/depth/ownerは一致する必要があります。

`retainedBinBytes` now includes private and merged pointer capacities, vector headers and touched/active-worker
lists, including idle workers' retained capacity. Earlier versions counted fewer categories: the figures are
**not like-for-like memory totals**. Allocator bookkeeping and RSS are still excluded. `binReferences` counts
each logical triangle-to-tile reference once, not both copies. `rasterMs` includes merge/sort, but these
application timers remain excluded from performance judgments.

Separate correctness fix: `BeginCommand` now checks shaded-vertex and stamp sizes independently. If allocation
of the second vector failed after growing the first, a later call previously skipped stamp growth. A regression
test constructs that partial-growth state without inducing real OOM. Additional tests exercise equal-depth,
different-material submissions, mixed wire/fill, all sorting/Hi-Z/hidden-wire combinations, empty frames,
resize and independent concurrent renderers.

#### Paired fill captures (Release x64, 1280×720, sorting/Hi-Z/stats OFF)

Each pair runs the preserved old binary first, then candidate4, under VS and VTune separately. A 6,000-frame
workload follows the two 1,200-frame pairs; its longer rotation sequence is not compared directly with a
short run. Whole-process elapsed is **not frame time**. VS shares are matched EXE self-IP samples, not machine
utilization. The reported application `BENCH` timings are not used.

| Pair | VTune elapsed: old → candidate4 | VTune effective CPU: old → candidate4 | VS busiest EXE thread share: old → candidate4 |
|---|---:|---:|---:|
| 1,200 frames #1 | 9.749 → 9.535 s | 44.362 → 44.146 s | 5.933 → 5.440% |
| 1,200 frames #2 | 10.462 → 10.170 s | 41.712 → 44.570 s | 5.575 → 5.329% |
| 6,000 frames | 53.859 → 51.872 s | 202.659 → 211.087 s | 5.975 → 5.178% |

**한국어:** 세 짝 비교에서 전체 시간은 각각 약 2.2%, 2.8%, 3.7% 낮았습니다. 긴 비교의 유효 CPU 시간은
약 4.2% 높아졌으므로 CPU 효율 개선이라고 부르지 않습니다. 첫 VS capture의 `binTriangle` self sample은
13,324/76,465에서 3,738/71,558로 줄었고, 긴 비교에서도 75,149/438,068에서 21,112/401,204로 줄었습니다.
이는 공유 삽입 비용을 줄인 근거지만, 해당 함수 비중만큼 프레임이 빨라진다는 뜻은 아닙니다.

**English:** Whole-process elapsed was approximately 2.2%, 2.8% and 3.7% lower in the three pairs. Effective
CPU rose about 4.2% in the long pair, so this is not a CPU-efficiency improvement claim. VS `binTriangle`
self samples fell from 13,324/76,465 to 3,738/71,558 in the first pair and from 75,149/438,068 to 21,112/401,204
in the long pair. This supports reduced shared-insertion cost, not an equivalent percentage of frame speedup.

**日本語:** 全体時間は3組で約2.2%、2.8%、3.7%短くなりました。ただし長い比較の有効CPU時間は約4.2%
増えたため、CPU効率改善とは主張しません。VSの`binTriangle` self sampleは最初の組で13,324/76,465から
3,738/71,558、長い組で75,149/438,068から21,112/401,204へ減少しました。共有挿入コストの削減を示しますが、
関数の比率と同じ割合でframeが高速化したという意味ではありません。

Short-run final hashes match (`4360798350140288590`); long-run hashes also match (`7411601588084696`). These
are last-frame hashes, supplemented by independent rasterizer and deterministic pixel/depth/owner tests.
Evidence folders: `fill-before4`, `candidate4`, their `-repeat` and `-long` variants under
`tests/bin/profiles/goal-opt-20261005/`. EXE/PDB/DLL/assets are frozen with manifests; all completed captures
have successful target/profiler exit records. The pre-change four renderer files are also saved in
`fill-before4/source/`.

Limits: pairs are sequential rather than randomized, power/temperature/background use are uncontrolled,
and reusable MSBuild workers are recorded but not proven idle. An initial attempt to start candidate4-long
was rejected because external `cl`/MSBuild processes were active; those processes were left untouched,
their termination was observed, and a fresh capture was then started. The guard is a start-time check,
not a continuous guarantee of exclusive machine use. This remains directional evidence, not a statistical
confidence claim. Candidate4 normal/wire/high-resolution performance comparisons are still outstanding.

Candidate4 validation: all four incremental Debug/Release × x64/Win32 builds and runtime suites passed,
with zero build warnings/errors; x64 Debug includes ASan. No clean rebuild or interactive visual sign-off is
claimed. 한국어: 네 구성 증분 빌드·회귀 테스트가 경고/오류 없이 통과했고 x64 Debug ASan도 통과했습니다.
日本語: 4構成の差分build・回帰testとx64 Debug ASanは警告・エラーなしで成功しました。

Capacity diagnostic, separate from performance measurements: a 1,200-frame instrumented fill run reported
67,384,688 retained bin bytes (about 64.26 MiB) for candidate4, with 154,819 live logical references in its
last frame. A separate old-binary run reported 6,992,224 bin bytes (about 6.67 MiB), but excludes metadata
categories added by candidate4. These are capacity sums, **not RSS or like-for-like allocator totals**, and
worker assignment can change retained high-water marks. Application timer fields from these runs are not
used. The worker-private per-tile vectors retain substantial historical capacity; reducing that storage
is an outstanding experiment, rather than declaring the modest elapsed gain a completed optimization.
한국어: 이번 계측의 candidate4 bin 보유 용량은 약 64.26 MiB입니다. 이전 값은 헤더 등의 집계 범위가
달라 단순 배수 비교할 수 없습니다. worker별 타일 벡터의 과거 최대 용량을 줄이는 실험이 남았습니다.
日本語: 今回のcandidate4のbin保持容量は約64.26 MiBです。旧値とはheader等の集計範囲が異なるため
単純な倍率比較はできません。worker別tile vectorのhigh-water容量削減も未完了です。

### Candidate5 — flat references / 연속 타일 참조 / 連続tile参照

**한국어:** worker마다 타일별 vector를 유지하던 구조를 flat `(tileIndex, triangle*)` 배열과
count/cursor로 변경했습니다. Frontend join 뒤 overflow를 검사하며 prefix offset을 만들고,
worker별로 서로 겹치지 않는 구간에 scatter합니다. Join 뒤 각 tile span을 정렬·래스터화합니다.
삼각형 풀, 수식, primitive ID, 기본 X-ray/선 패스는 유지합니다. Prefix와 scatter는 추가 비용이므로
메모리 절약을 곧바로 속도 향상으로 해석하지 않습니다.

**English:** Worker-private per-tile vectors became flat `(tileIndex, triangle*)` arrays with counts/cursors.
After the frontend join, a checked prefix assigns disjoint scatter ranges. Tile spans are sorted/rasterized
only after the scatter join. Triangle pools, arithmetic, primitive IDs and default X-ray/line paths remain
unchanged. Prefix/scatter costs must be measured; reduced storage is not itself a throughput improvement.

**日本語:** worker別のtile vectorをflatな`(tileIndex, triangle*)`配列とcount/cursorに変更しました。
Frontendのjoin後、overflowを検査するprefixで重ならないscatter区間を割り当て、そのjoin後に
tile spanをsort/rasterizeします。三角形pool・数式・primitive ID・既定X-ray/線経路は維持します。
Prefix/scatterにも費用があるため、メモリ削減だけで高速化とは判断しません。

Capacity diagnostic only: a separate 1,200-frame fill run with counters enabled reported **10,299,456 bytes
(9.82 MiB)** of retained bin storage, versus candidate4's earlier 67,384,688 bytes (64.26 MiB).
These are per-run container-capacity sums, not RSS; worker high-water marks depend on scheduling.
Candidate5 includes flat references, final pointers, count/cursor/offset storage, headers, touched/active
lists and wire/debug bins. The last frame had 154,819 logical references, 116,634 prepared triangles,
68,282 filled pixels and pixel hash `4360798350140288590`. Application timer fields are not performance evidence.

Validation: all four incremental Debug/Release × x64/Win32 builds and runtime suites passed with zero
warnings/errors; x64 Debug uses ASan. New tests cover prefix cardinality/order, exactly-once scatter,
empty/reset transitions, checked overflow without huge allocation, retained capacity accounting,
same-tile-count viewport shape changes and Fill→X-ray→Fill. Existing single/multi-worker, renderer
isolation, depth/owner, hidden-wire, normal and menu tests remain enabled. Interactive visual sign-off
and a fresh clean rebuild are not claimed.

An initial 6,000-frame fill pair (`fill-before5`, `candidate5`) reported VTune whole-process elapsed
46.456→47.329 s and effective CPU 198.871→214.770 s. The same candidate4 binary varied from 51.872 s
in an earlier run to 46.456 s here. A newly resident MSBuild node was observed between capture phases;
its existence does not prove an overlapping build or identify a cause. This pair is **not clean evidence
of a speedup or regression**. The user reported no separate builds or heavy tasks.

#### Three-mode raw profiler comparison (not environment-verified)

The `before5-modes` and `candidate5-modes` folders preserve candidate4/candidate5 EXE/PDB/DLL/assets.
Both used the same capture/monitor script hashes, Release x64, 1280×720, 60 warmup + 1,200 measured
rotating frames, sorting/Hi-Z/stats/hidden-wire OFF. VS and VTune ran separately, without our builds/tests.
Every target/profiler exited zero. Because build-start monitoring was unavailable, both manifests retain
`valid_for_comparison=false`: these are raw observations, **not a controlled or statistically established
speedup/regression**. Whole-process elapsed includes startup/warmup/shutdown and is not frame time.
VS shares count matched-EXE self-IP samples only, excluding TBB/system DLLs; they are not CPU utilization.

| Mode | VTune elapsed: candidate4 → candidate5 | Effective CPU: candidate4 → candidate5 | VS busiest EXE thread share: candidate4 → candidate5 |
|---|---:|---:|---:|
| Fill | 12.311 → 10.767 s | 44.520 → 47.732 s | 5.646 → 6.184% |
| Normals | 18.438 → 20.038 s | 49.122 → 36.322 s | 10.058 → 10.246% |
| Default X-ray wire | 12.321 → 12.051 s | 19.299 → 19.092 s | 7.220 → 7.601% |

Final pixel hashes match per mode: fill `4360798350140288590`, normals `16630578814480127575`,
wire `504747146023326269`. Hashes verify the last frame, not every profiled frame; independent regression
tests cover repeated pixel/depth/owner equality. The flat implementation remains a tested working-tree
experiment, not a finalized throughput win. Normal-mode repeat comparisons and higher-resolution
performance remain outstanding; no app `BENCH` timing is used for this decision.

한국어: 이번 원시 기록은 채움·와이어 시간은 낮고 법선 시간은 높습니다. 메모리 보유량은 줄었지만
모든 모드의 속도 향상이나 worker 분포 개선은 확정하지 않습니다. 법선 재측정과 고해상도 검증이 남았습니다.
日本語: 今回の生データではfill/wire時間が短くnormal時間は長くなりました。保持メモリは減りましたが、
全モードの高速化やworker分布の改善は確定していません。Normalの再比較と高解像度の検証が残っています。

### Candidate6 — frame-local transform reuse / 프레임 내 변환 재사용 / frame内変換の再利用

**한국어:** candidate5의 VS normal 캡처에서 `shadeVertex`가 EXE self-IP 101,910개 중 11,042개였고,
VTune에서는 `cachedVertex`/`processCommand`/`shadeVertex`의 effective CPU가 각각 1.110/0.769/0.246 s였습니다.
이는 도구별 인라인·귀속 방식이 다른 별도 관측이며, 이 비율만큼 빨라진다는 예측이 아닙니다.
옥트리가 같은 메시·변환을 index 부분집합 명령으로 나누는 점을 확인하고 worker-local 단일 entry로
정점 stamp·MVP·법선 행렬을 재사용했습니다. 키는 mesh identity, vertex data/size, world/VP의 32개 float
비트와 mode입니다. 프레임 시작에는 반드시 무효화하고, miss/기존 size-only API는 stamp를 갱신합니다.

**English:** A single-entry worker-local cache reuses vertex stamps, MVP and normal matrices across exact
transform-key matches within one frame. The key includes mesh identity, vertex data/size, all world/VP float
bits and mode; materials/index subsets do not affect vertex transforms. Frame reset invalidates it, and
misses/legacy size-only calls advance the stamp. No global map, frame-crossing cache or task redistribution
was introduced. Independent storage-size checks and publish-after-success preserve allocation recovery.

**日本語:** 同一frame内で完全一致する変換keyだけをworker-localな単一entryで再利用します。
Mesh identity・vertex data/size・world/VPの全float bit・modeを含め、material/index部分集合は除きます。
Frame開始で必ず無効化し、miss/従来APIではstampを更新します。全体map、frame間cache、task再分配は
追加せず、独立resizeと成功後のkey公開を維持します。

All four incremental configurations passed with zero warnings/errors (x64 Debug ASan included). Tests
verify bitwise agreement with direct matrix/vertex computation, actual slot reuse, material/subset hits,
world/nonuniform/singular/VP/camera/mesh/storage/size/mode misses, signed-zero/NaN bit keys, same-address
vertex mutation after frame reset, independent workers, stamp wrap, partial-growth repair and unchanged
per-command material ownership/primitive IDs. These tests establish cache correctness, not a speedup.

#### Raw comparison and reverse-order baseline repeat

`candidate5-modes` → `candidate6-modes` → `before6-repeat-modes` used preserved binaries and identical guard
hashes, 1,200 measured rotating frames at 1280×720, Release x64, 60 warmup frames, options/stats OFF.
Each tool ran separately. All three modes' last-frame hashes match the candidate5 table. Both profiler
launches and targets completed successfully. Build-start monitoring remained unavailable; manifests
retain `valid_for_comparison=false`. No statistical or controlled-environment claim is made.

| Mode | VTune elapsed: v5 → v6 → v5 repeat | Effective CPU: v5 → v6 → v5 repeat | VS busiest EXE share: v5 → v6 → v5 repeat |
|---|---:|---:|---:|
| Fill | 10.767 → 10.878 → 12.030 s | 47.732 → 46.434 → 42.569 s | 6.184 → 5.785 → 6.870% |
| Normals | 20.038 → 19.446 → 20.980 s | 36.322 → 45.280 → 42.419 s | 10.246 → 9.184 → 9.723% |
| Default X-ray wire | 12.051 → 13.438 → 14.368 s | 19.092 → 23.307 → 22.596 s | 7.601 → 6.942 → 7.135% |

Elapsed includes startup/warmup/shutdown; EXE self-IP shares exclude TBB/system DLLs and are not machine
utilization. Fill `shadeVertex` self samples were 10,880 → 10,083 → 11,725; normal samples were
11,042 → 10,214 → 11,006. This is evidence to continue evaluating reduced transform work, not proof of
overall throughput improvement. The repeat baseline also shifted substantially. Candidate6 remains a
tested working-tree experiment; a sustained frame-rate gain and final adoption are **not established**.
한국어: 정점 변환 샘플은 줄었지만 실행 시간 변동이 커서 전체 속도 개선·최종 채택은 미확정입니다.
日本語: 頂点変換sampleは減少しましたが時間変動が大きく、全体高速化と最終採用は未確定です。

The candidate5 VTune `threads-functions.txt` report additionally attributes main-thread effective CPU
0.914 s to `renderDepthTestedLines`, 0.510 s to prepared-optional-vector `resize`, and 0.428 s to index-vector
`push_back`. Removing redundant prepared-slot initialization is the next isolated experiment, not a claim
that these entire sample totals can be saved. The repeatable grouping command is:

```powershell
vtune -report hotspots -result-dir <capture>/vtune-normal -group-by thread,function
```

### Candidate7 — prepared debug-slot reuse / 법선 준비 슬롯 재사용 / 法線準備slotの再利用

**한국어:** 큰 법선 묶음에서 `clear()` 직후 같은 크기로 `resize()`하던 중복 초기화를 제거했습니다.
현재 개수로 정확히 resize하고 모든 슬롯에 준비 결과 또는 `nullopt`를 병렬 대입한 뒤 join하므로
이전 결과를 읽지 않습니다. 작은/빈 debug batch만 clear하며, 명령·chunk·bin은 매 프레임 비웁니다.
계산·순서·선 개수·깊이 정책·행 분할은 바꾸지 않았고 candidate6의 캐시는 그대로입니다.

**English:** Large normal batches retain their optional slots and resize to the exact current line count;
unconditional preparation overwrites every slot, including rejected lines with `nullopt`, before the join
and bin/draw phases. Small/empty batches clear logical slot size. Commands/chunks/bins still clear each
frame. This removes redundant initialization without changing line arithmetic, order, depth policy or bands.

**日本語:** 大きい法線batchでは準備slotを毎回clearせず、現在の線数に正確にresizeします。全slotに
結果または`nullopt`を無条件代入し、join後にだけ読みます。小さい/空batchの論理サイズと、毎frameの
command/chunk/binは従来どおり消去します。線の数式・順序・深度・band分割は変更していません。

All four incremental builds/runtime suites passed with zero warnings/errors, including x64 Debug ASan.
`preparedDebugSlotFreshnessTests` compares an independent sequential drawing path through sizes
769→256→257→255→0→769 and valid→all-NaN→new-color/MVP valid→all-clipped→valid transitions at each size.
It exercises both line algorithms, one/default workers, stats ON/OFF, odd tails, ignored non-Line commands,
overlays and a fully empty final frame. Pixels/depth/owners, rejection counts, visible-color sentinels and
small/empty live-byte behavior are checked. No clean rebuild or manual visual sign-off is claimed.

#### Isolated normal comparison: 3,000 measured frames

`before7-normal3000` preserves candidate6; `candidate7-normal3000` preserves candidate7. Both use Release
x64, 1280×720, 60 warmup + 3,000 measured rotating normal frames, options/stats OFF and identical guard
hashes. VS and VTune are separate runs; all targets/profilers exited zero. Last-frame pixel hashes match
(`7020453443417948241`). These longer workloads are not compared directly with 1,200-frame elapsed values.

| Metric | candidate6 → candidate7 |
|---|---:|
| VTune whole-process elapsed | 48.215 → 49.003 s |
| VTune effective CPU | 106.584 → 100.925 s |
| VS busiest matched-EXE self-IP thread share | 9.248 → 8.219% |
| VTune `renderDepthTestedLines` effective CPU | 2.563 → 2.636 s |
| VTune prepared-optional-vector `resize` standalone entry | 1.327 s → no separate entry in report |

The absent standalone resize entry is consistent with the source change, **not proof of zero allocation
or zero inclusive cost**. Effective CPU and busiest EXE share were lower in this pair, but elapsed was
higher; neither sample distribution nor one CPU total establishes a frame-throughput gain. Build-start
monitoring was unavailable (`valid_for_comparison=false`), and power/scheduling/background behavior was
not fully controlled. CPU classifications and sampling/inlining differ by tool. Candidate7 is retained as
a correctness-tested reduction of redundant work, not a completed whole-renderer speedup. Smaller row
bands are a subsequent load-distribution experiment; they have not been implemented in this version.
한국어: 이번 짝 비교의 CPU·최대 스레드 비중 감소와 달리 전체 시간은 증가했습니다. 전체 속도 개선은
확정하지 않으며, 행 구간 분할에 따른 병렬 작업 분포를 추가로 검토합니다.
日本語: 今回はCPU・最大thread比率が減少した一方で全体時間は増加しました。全体高速化は確定せず、
次に行bandの分割と作業分布を検討します。

### Candidate8 — exact Bresenham row traversal / 행 구간 순회 / 行区間の走査

**한국어:** 선을 맡는 band 수는 그대로입니다. 기존에는 K개 band를 가로지르는 선을 각 band가
처음부터 끝까지 순회하고 범위 밖 쓰기만 거부했습니다. Bresenham은 원래 시작점 기준 major step에서
좌표와 정수 오차를 복원해 담당 행의 첫 점부터 마지막 점까지만 순회합니다. 끝점을 다시 clip하지
않으므로 tie, 역방향의 비대칭, 원래 float 끝점을 사용하는 깊이 보간은 유지됩니다. DDA의 반복
double 덧셈은 변경하지 않았습니다. 허용된 큰 좌표에서 `2*error`가 int32를 넘던 문제는 int64로
별도 수정했습니다. 구역 세분화만으로 이득을 가정하지 않습니다.

**English:** Band count is unchanged. Previously each of K intersecting bands traversed the entire line,
rejecting writes outside its rows. Bresenham now restores the original major-step coordinate/error state
and visits only its owned first-to-last points. It does not reclip endpoints, preserving ties, reversed-line
asymmetry and depth interpolation from the original float endpoints. DDA retains sequential double
addition. Widening error and doubled error to int64 separately fixes overflow for accepted extreme inputs.

**日本語:** Band数は変更していません。各bandが線全体を再走査する代わりに、Bresenhamの元の
始点に対するmajor stepから座標と整数誤差を復元し、担当行だけを走査します。端点を再clipせず、
tie・逆方向の非対称性・元のfloat端点による深度補間を保ちます。DDAのdouble逐次加算は維持し、
許容される大座標での`2*error` overflowはint64化で別途修正しました。

An independent integer calculation checked the original recurrence against the restricted traversal for
682,344 row intervals, including all octants and endpoint reversal. `independentLineTraversalTests` uses
the original full DDA/Bresenham recurrence, not the optimized production path, plus an independent depth
plot oracle. It covers fractional/offscreen/tie inputs, empty/reversed/clamped bands, stats, padding and
unchanged depth/owners. Four extreme Y-major cases use an analytically known midpoint tie and generate
only visible points; the unchanged DDA never receives those billion-step inputs. These checks establish
correctness, not speed. All four incremental builds/runtime suites passed with zero warnings/errors,
including x64 Debug ASan. The profiler comparison is recorded below; no manual visual sign-off is claimed.

#### Candidate7 → candidate8 raw comparison (1,200 frames)

The first `before8-modes` attempt was aborted between collections when the prelaunch check found a
`link` and a non-resident `MSBuild` process. They ended before inspection; their origin and overlap with
the completed VS fill capture are unknown. This attempt is excluded, preserved and not silently reused.
Neither process was terminated by this task. The user reported no manually initiated build/heavy work.

`before8-modes-retry` then completed against the immutable candidate7 snapshot, followed by
`candidate8-modes`. Both use Release x64, 1280×720, 60 warmup + 1,200 measured rotating frames,
options/stats OFF, default Bresenham and identical guard hashes. All six targets/launchers per version
exited zero. Both manifests remain **`valid_for_comparison=false`**: build-start subscriptions were
unavailable, and a reusable MSBuild node was noted but not proved idle. These are provisional raw
observations, not controlled evidence of a sustained throughput gain.

| Mode | VTune whole-process elapsed | VTune effective CPU | VS busiest matched-EXE self-IP share |
|---|---:|---:|---:|
| fill | 15.692 → 13.020 s | 50.366 → 60.069 s | 7.202 → 5.761% |
| normal | 22.421 → 20.151 s | 46.597 → 38.320 s | 9.838 → 8.862% |
| default X-ray wire | 12.640 → 13.105 s | 23.891 → 17.637 s | 7.196 → 7.690% |

Last-frame VTune target hashes match: fill `4360798350140288590`, normal `16630578814480127575`,
wire `504747146023326269`. Normal line-loop self-IP samples in VS changed from 9,772 to 8,769, and wire
from 7,901 to 5,289. The new row-state helper also costs work: 699/1,171 VS self-IP samples in normal/wire
and 0.247/0.345 s standalone VTune effective CPU, plus separately attributed helper lambdas. VTune's
line-loop entries changed from 1.560 to 1.274 s in normal and 2.713 to 1.654 s in wire. These entries are
not inclusive call totals, and a new helper can shift attribution; they cannot be converted directly to FPS.

The fill control also moved considerably despite not exercising the optimized normal/wire workload.
Normal elapsed and effective CPU were lower in this pair; wire effective CPU was lower but elapsed was
higher. Do not infer universal improvement or equal worker load from this table. The source removes
provably redundant row traversal and fixes a tested overflow, but broader performance acceptance still
requires repeated alternating captures. Band count remains unchanged; smaller bands and a cheaper
contained-line helper path have **not** been implemented or claimed as wins.

한국어: 법선은 이번 짝 비교에서 시간·CPU가 줄었지만 와이어의 전체 시간은 늘었습니다. 채움 대조군도
크게 변했으므로 전체 FPS 개선이나 모든 worker의 균등 실행을 확정하지 않습니다. 숨은 선 옵션은
정확성 테스트에 포함했지만 이번 프로파일은 기본 X-ray wire만 측정했습니다.
日本語: 今回の法線では時間・CPUが減少しましたが、wireの全体時間は増えています。Fill対照群も
大きく変動しており、全体FPS改善やworkerの均等実行は確定しません。隠線optionは正当性テストに
含みますが、今回のprofileは初期値のX-ray wireだけです。

#### Build-start observation limits

`CaptureCpuProfiles.ps1` now records build-process starts through the scoped
`BuildInterferenceMonitor.ps1` helper. Detected starts invalidate a run after preserving its artifacts.
Existing MSBuild CPU deltas are advisory, never proof of idle hardware. Only this run's GUID subscription
and process handles are cleaned up; unrelated builds are never terminated.

This Windows session denied the `Win32_ProcessStartTrace` subscription. The default path therefore
stops before launching a profiler. Explicit `-AllowUnverifiedBuildMonitor` permits raw collection and
analysis while **retaining `valid_for_comparison=false`** in reports and manifests. It cannot override
an actually detected build. No privileges or system settings were changed. Both comparison sides must
use the same guard; manifests record helper/capture-script SHA256 hashes. Even a supported guard with no
events cannot prove the absence of other CPU activity or events delivered after its bounded final drain.

Verified monitor checks: unavailable subscription, scoped cleanup, repeated Stop, strict pre-launch
rejection and report preservation. Override/invalid-event policy was tested with synthetic reports.
Actual quiet/short-lived-build detection in a supported environment remains **untested**.

한국어: 중간 빌드 감시는 이 환경에서 권한 문제로 미검증입니다. 허용 옵션으로 수집하더라도 검증된
비교로 표시하지 않으며, 별도 작업이 없다는 사용자 답변만으로 환경의 완전한 통제를 주장하지 않습니다.
日本語: この環境では権限不足で実行中のbuild監視を検証できません。明示optionで収集しても検証済み
比較とは記録せず、他の作業がないという申告だけで完全に制御された環境とは主張しません。

Reproduce after building, with no other build/profiler/benchmark running:

```powershell
pwsh -File tests/CaptureCpuProfiles.ps1 -OutputDirectory tests/bin/profiles/new-capture-001 -Analyze
```

The output directory must be new. `-HiddenWireframe` selects the optional hidden-line policy. The script captures
VS and VTune sequentially, then resolves VS ETL samples against the exact PDB and generates VTune reports.
Reusable MSBuild nodes are recorded as a warning, not proof of idle hardware. `RunTests.ps1` now uses `/nr:false`
to avoid leaving new reusable build nodes. No unrelated process is stopped to manufacture an idle system.

<a id="renderer-visibility-2026"></a>
## Render-target separation and visibility (2026-10-05)

### 한국어 — 이번 변경

- `Renderer`는 Win32/GDI를 포함하지 않고 `RenderTarget`에 그립니다. 색상은 stride가 있는
  비소유 `PixelSurfaceView`, 깊이와 primitive ID는 target 소유입니다. `GdiPresenter`가 DIB를
  소유하고 그 메모리를 직접 빌려주므로 분리로 인한 전체 화면 복사는 없습니다.
- 삼각형의 viewport·보간·edge 준비를 한 번 수행하고 여러 타일이 공유합니다. 안정된 제출 순서와
  primitive ID로 같은 깊이에서는 작은 ID가 이깁니다. 클리핑은 perspective divide보다 먼저 합니다.
- 채움은 한 번의 depth-tested shaded pass입니다. **기본 wire는 빠른 X-ray 표시**로 타일 등록과
  면 깊이 패스를 생략합니다. `hiddenWireframe`을 켜고 wire가 하나라도 있으면 모든 면의 depth prepass
  후 채움 명령만 owner가 일치하는 픽셀에 색을 씁니다. 그 뒤 wire·법선을 깊이 검사하고,
  AABB·광원 오버레이, FXAA, Present 순서입니다. 법선은 독립적으로 존재하는 fill 깊이를 검사합니다.
  숨은 wire는 과거의 투과 wire와 시각적으로 다르며, 비용 측정 후 사용자 선택에 따라 옵션으로 분리했습니다.
- 앞→뒤 정렬과 현재 프레임의 tile Hi-Z는 각각 독립 옵션이며 기본 OFF입니다. Hi-Z는 실제 채워진
  4×4 block과 16×16 tile의 최소 `1/w`만 사용합니다. 구멍·동일 깊이·subnormal·불확실한 경계에서는
  기각하지 않습니다. 객체 전체나 이전 프레임의 occlusion culling은 아닙니다.
- `collectStats`도 기본 OFF입니다. coverage/depth/shading/Hi-Z/line counters와 단계 시간을 선택적으로
  수집합니다. 계측 실행과 최종 wall-time 측정은 구분합니다.
- 깊이 선은 현재 픽셀이 이미 같은 색이면 추가 계산을 생략합니다. 깊이/owner를 쓰지 않는
  같은 행의 쓰기 순서를 유지하므로 최종 결과가 정확히 같습니다. 계측 ON은 이 생략을 하지 않으므로 기각 횟수는
  기준 경로의 논리적 시도 횟수이며, 계측 OFF의 실제 수행량과 같다고 해석하지 않습니다.
- 비용도 있습니다. 1920×1080의 64-bit primitive ID만 약 15.82 MiB이고, `PreparedTriangle`과
  worker별 high-water capacity가 추가됩니다. 숨은 wire 옵션은 면 깊이 패스가 필요합니다. Hi-Z는 기각되지 않은
  삼각형마다 tile 요약을 다시 읽기 때문에 겹침이 적은 장면에는 불리할 수 있습니다.
- `Present`의 `BitBlt` 뒤에 `GdiFlush`로 CPU의 다음 DIB 쓰기와 동기화합니다.
  근거: [CreateDIBSection](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection),
  [GDI batching](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush).
  FXAA의 기존 red/blue 휘도 해석 불일치는 구조 변경과 분리하기 위해 이번에 바꾸지 않았습니다.

### English — this change

Rendering now targets a non-owning, strided pixel view with owned depth/primitive IDs; the GDI presenter
owns and lends the DIB directly, without an extra frame copy. Prepared triangles are reused across tiles.
Stable IDs resolve equal-depth ties. Default wireframes retain fast X-ray display and skip wire bins/depth prepass.
Optional `hiddenWireframe` enables a whole-scene depth prepass and depth-tested wires. Normals independently
read available fill depth; AABBs/lights remain overlays. Hidden wires intentionally differ from the old see-through view.
Front-to-back sorting, current-frame tile Hi-Z, and statistics are independent, default-OFF options.
Hi-Z uses fully covered blocks, strict conservative depth bounds, and does not perform temporal/object culling.
IDs add 15.82 MiB at 1080p, prepared-data capacities also cost memory, and hidden-line/Hi-Z work is not free.
Depth-tested lines skip already-identical colors without changing depth/owners; instrumented runs retain
the reference work so their rejection counts describe logical attempts, not fast-path executions.
`GdiFlush` completes presentation before CPU reuse. The historical FXAA channel interpretation is unchanged.

### 日本語 — 今回の変更

Rendererはstride付きの非所有pixel viewと、depth/primitive IDを所有するRenderTargetに描画します。
GDI presenterのDIBメモリを直接借りるため、分離による画面全体の追加コピーはありません。
準備済み三角形をtile間で共有し、安定したIDで同じdepthの勝者を決めます。
既定wireは高速なX-ray表示で、wireのbin/depth prepassを省略します。`hiddenWireframe`をONにした場合のみ
全ての面のdepth prepassとwire深度判定を行います。法線は利用可能なfill深度を独立に読み、AABB・光源はoverlayです。
隠線wireは従来の透過表示と意図的に異なります。前後sort・現フレームtile Hi-Z・統計は独立した既定OFFの機能です。
Hi-Zは完全に覆われたblockだけを保守的に判定し、前フレームや物体単位のcullingではありません。
1080pのIDだけで約15.82 MiBを使い、準備データ・隠線処理・Hi-Zにもコストがあります。
深度判定線は既に同色のpixelを省略できます。計測ONでは基準処理を残すため、counterは論理的な試行数です。
Present後の`GdiFlush`でCPUの再利用と同期します。既存のFXAAチャンネル解釈は変更していません。

### Measured tradeoffs / 측정된 비용 / 測定結果

Before making hidden wires optional, five alternating runs per version used 60 warmup + 600 measured frames,
real rotating IronMan/teapot, a fitted fixed camera, and the hidden-window Framework/GDI path. Statistics were
OFF; no builds or profilers ran concurrently. Values are the median of five run means (and separately of five
run p95s), **not** statistics over pooled frames. Power, temperature and unrelated background load were not controlled.

| Resolution / Mode | Previous stage mean → hidden-line stage mean | p95 before → after |
|---|---:|---:|
| 1280×720 fill | 7.91 → 9.68 ms | 11.56 → 14.15 ms |
| 1280×720 normals | 14.70 → 24.30 ms | 23.25 → 36.36 ms |
| 1280×720 hidden wire | 12.29 → 30.96 ms | 20.20 → 49.62 ms |
| 1920×1080 fill | 10.14 → 13.43 ms | 14.58 → 18.89 ms |
| 1920×1080 normals | 17.45 → 29.69 ms | 25.13 → 41.84 ms |
| 1920×1080 hidden wire | 15.52 → 39.47 ms | 25.89 → 60.90 ms |

한국어: 이 단계는 **전체 성능 개선에 성공한 결과가 아닙니다.** 버퍼/ID/안전 검사 비용이 있고,
법선·숨은 wire는 기존에 없던 깊이 검사를 수행합니다. wire 전후는 표시 의미도 다릅니다.
사용자는 이 결과를 보고 기본 wire를 빠른 X-ray로 유지하고 숨은 wire를 선택 옵션으로 정했습니다.
위 표의 wire는 따라서 **최종 기본 모드가 아닙니다**. 최종 X-ray 측정은 별도로 기록합니다.

English: This stage is **not an across-the-board speedup**. Ownership/ID/safety work costs time, and hidden
lines perform additional depth work. Wire images also have different semantics. Following these measurements,
the user chose fast X-ray as the default and hidden wire as an option; the wire rows above are **not the final default**.

日本語: この段階は**全モードの高速化に成功した結果ではありません**。ID・安全検査と隠線depth処理の費用があり、
wireの表示も異なります。測定後のユーザー指定で高速X-rayを既定、隠線wireを選択式にしました。
上表のwireは**最終既定モードではありません**。

#### Final default X-ray wire / 최종 기본 와이어 / 最終既定wire

After the option split, another five alternating runs per version used the same 60 warmup + 600 measured
frames with statistics OFF. These are again medians of run means and run p95s, not pooled-frame statistics.

| Resolution | Previous stage mean → final X-ray mean | p95 before → after | Final pixel hash (both versions, all five runs) |
|---|---:|---:|---:|
| 1280×720 | 11.93 → 17.98 ms | 20.16 → 29.55 ms | 15565534381008786788 |
| 1920×1080 | 14.00 → 20.50 ms | 22.25 → 36.18 ms | 609602192449162330 |

한국어: 기존 투과 와이어의 최종 픽셀은 복원했지만 **기존 속도까지 복원한 것은 아닙니다**.
이 측정의 평균 프레임 시간은 이전 단계보다 약 51%/46% 늘었습니다. 숨은 선 비용은 선택 옵션으로
분리했으며, 남은 성능 회귀의 원인을 최종 기본 경로에서 다시 프로파일링해야 합니다.

English: Final X-ray pixels match the old display, but **old performance is not restored**. Mean frame time
increased by about 51%/46% in these runs. Hidden-line work is optional; the remaining regression needs a new
profile of the final default path. The earlier hidden-line profiles do not establish its cause.

日本語: 最終pixelは従来の透過wireと一致しますが、**従来の速度は回復していません**。
この測定では平均frame時間が約51%/46%増加しました。隠線処理は選択式ですが、残る性能低下は
最終既定経路で再profileが必要です。以前の隠線profileだけでは原因を確定できません。

Raw ignored directories: `visibility-real-1280x720-20261005-141020-xray`,
`visibility-real-1920x1080-20261005-141201-xray`. Only the last frame is hashed by this benchmark;
matching hashes here do not assert that every intermediate frame was compared.

Five runs of the 1280×720 memory-only synthetic fill benchmark used 60 warmup + **120** measured frames:

| Scenario | Neither | Sort only | Hi-Z only | Sort + Hi-Z |
|---|---:|---:|---:|---:|
| 96 overlapping layers + front wall | 20.63 ms | 15.73 ms | 18.17 ms | 7.96 ms |
| 96 separated quads | 3.31 ms | 3.23 ms | 3.19 ms | 3.03 ms |

Each scenario retained the same final pixel hash across every option/run. Combined sorting/Hi-Z helped the
heavily occluded scene; small, overlapping timing distributions on the separated scene do not justify a universal
default. These are memory-only synthetic scenes, not application FPS. Counter zeros in non-instrumented logs mean
collection was OFF, not that no work occurred. The real-scene before-fill hashes varied between two values; current
hashes stayed fixed within each mode, with fill matching one baseline value. Normal/wire hashes intentionally differ.

Final instrumented spot checks (120 measured frames, 1280×720) retained the same real-fill last-frame hash
`11397697079266144776` for neither/sort/Hi-Z/both. Last-frame shading attempts fell from 146,270 to 84,322
with sorting, yet the extra work did not improve this run's frame time. Combined options rejected 76,034
triangle/tile references. For the occluded synthetic scene, coverage attempts fell from 15,902,365 to 673,920
and shading attempts from 1,968,650 to 673,920, retaining hash `5213098462387823443`. These counters describe
one final frame, not all frames or unique triangles. Instrumented timings are not the uninstrumented table above.

Raw ignored directories: `visibility-real-1280x720-20261005-134928`,
`visibility-real-1920x1080-20261005-135502`, `visibility-occluded-1280x720-20261005-140221`,
`visibility-open-1280x720-20261005-140320-xray`. This set predates the final default-mode switch.

### Validation / 검증 / 検証

- Core change: `RunTests.ps1 -Rebuild` passed for Debug/Release × x64/Win32. After the user's final X-ray-default
  selection, incremental builds and the expanded tests passed again in all four configurations, zero warnings/errors.
  x64 Debug also ran with the existing AddressSanitizer setting. Asset and oneTBB DLL deployment ran for both projects.
- Memory target validation covers zero/negative/overflow sizes, stride, padding, failed attachment, resize,
  and presenter lifetime. Independent raster references compare 200 triangles and 800 line cases.
- Visibility tests cover one-pixel holes, partial tiles, equal-depth ID ties, mixed fill/wire, camera-plane
  clipping, varied/extreme `1/w`, and instrumented/non-instrumented line equivalence. Synthetic/fixture renders
  repeat 100 times at single/default worker limits; IronMan/teapot command ordering also repeats 100 times.
  Real assets additionally run eight small rendering smoke cases each (including wire, normals and FXAA).
- This does **not** establish 100 full-resolution, full-asset frame comparisons across workers. Interactive
  visual checks, real visible-window latency, 4K, non-AVX hardware and exhaustive races remain unverified.

한국어: 코어 변경의 네 구성 클린 빌드·테스트와, 사용자 선택에 따른 최종 X-ray 옵션 추가 후 네 구성
증분 빌드·확장 테스트가 모두 통과했고 경고/오류는 0개입니다. 100회 반복 픽셀 비교는
합성/fixture 장면이며, 실제 두 자산은 100회 명령 순서 비교와 별도 소형 렌더 smoke test로 구분합니다.
실제 창을 조작하는 시각 회귀·4K·AVX 미지원 실기기까지 검증했다고 해석하지 않습니다.

日本語: コア変更の4構成clean build・testと、ユーザー指定の最終X-rayオプション追加後の4構成差分build・拡張testは
全て成功し、警告/エラーは0件です。100回のpixel比較は
合成/fixtureシーンであり、実資産は100回のcommand順序比較と別の小型render smoke testです。
実画面操作による視覚回帰・4K・AVX非対応実機は未検証です。

### Profile evidence / 프로파일 근거 / プロファイル根拠

The new hidden-line implementation was profiled before the final pixel-writer inlining change.
Both captures ran the wire benchmark for 1200 frames at 1280×720.

| Function / 작업 | VS sampled IP count¹ | VTune CPU seconds² |
|---|---:|---:|
| Depth-only triangle loop | 22,494 | 5.286 |
| Inner line pixel lambda | 14,468 | 13.027 |
| Outer line loop lambda | 4,410 | 4.312 |

¹ After a 5000 ms cutoff, filtering the benchmark PID and its EXE address range yielded 108,767 image samples.
These are self instruction-pointer counts, not inclusive call-tree percentages. ² VTune software Hotspots
includes startup/warmup/exit and sums thread CPU time. The two collectors are separate runs; neither number
is frame wall time. Wait/spin labels and missing vendor/system symbols do not prove lock contention.

한국어: 선 픽셀 본문이 인라인되지 않은 별도 함수로 남아 있음을 확인했습니다. 수식과 분기 순서를
바꾸지 않고 작은 `LinePixelWriter::__forceinline operator()`로 옮겼습니다. 이 표는 수정 전 병목의
근거이지, 표의 CPU 시간 전체가 함수 호출 비용이라거나 그만큼 빨라졌다는 뜻은 아닙니다.

English: The per-pixel lambda remained out-of-line. It was moved, without changing expression/branch order,
to a small explicitly inlined writer. The table identifies the pre-change hotspot; it does not attribute all
its CPU time to call overhead or claim an equivalent speedup.

日本語: pixel lambdaが個別の関数として残っていました。式と分岐順序を維持して小さな明示的inline
writerへ移しました。この表は修正前のhotspotであり、CPU時間全てが呼出し費用、または同量の改善という意味ではありません。

Local ignored evidence: `vs-visibility-wire-before-inline.diagsession`,
`vs-visibility-wire-before-inline-hotspots.txt`, `before-line-inline/RendererTests.{exe,pdb}`,
and `vtune-visibility-wire-final/` (despite its historical directory name, captured **before** the final inlining change).

### Reproduce this stage / 이번 단계 재측정 / 今回の再測定

```powershell
./tests/RunTests.ps1 -Rebuild
./tests/MeasureVisibility.ps1 -Runs 5 -Frames 600 -CompareBaseline
./tests/MeasureVisibility.ps1 -Runs 5 -Frames 600 -Width 1920 -Height 1080 -CompareBaseline
./tests/MeasureVisibility.ps1 -Runs 5 -Frames 600 -Modes wire -HiddenWireframe
./tests/MeasureVisibility.ps1 -Frames 120 -Scenario occluded -Modes fill -Options none,sort,hiz,both
./tests/MeasureVisibility.ps1 -Frames 120 -Scenario open -Modes fill -Options none,sort,hiz,both
./tests/bin/x64/Release/RendererTests.exe . --benchmark 600 1280 720 fill both stats
```

`CompareBaseline` requires the locally preserved, git-ignored `before-visibility` binary; it is not supplied
in a fresh checkout. Omit the switch to measure only the current binary. Do not run builds or profilers
concurrently with timing runs. Counters describe the last frame, not the sum of measured frames.

---

## Historical optimization before visibility / 이전 단계 기록 / 可視性変更前の記録

The numbers and build results below refer to the preceding optimization, not the new visibility implementation.

## 한국어

### 측정 조건

2026-10-05, Core Ultra 9 275HX(24 logical processors), Windows 11, MSVC v145,
Release x64, oneTBB 2023.1.0에서 측정했습니다. C++26 Preview, SSE/AVX dispatch,
기존 LTCG 비활성화 설정은 변경하지 않았습니다.

`RendererTests --benchmark`는 실제 Framework·Renderer·GDI 경로를 숨겨진 창에서
실행합니다. 별도의 headless renderer가 아닙니다. IronMan과 teapot의 초기 경계가
모두 들어오는 고정 카메라, 1280×720 client area, 고정 delta time(1/60초), 모델 회전을
사용합니다. 각 실행은 60프레임 준비 후 600프레임을 측정합니다.
fill/normal/wire 각각 변경 전·후 실행 파일을 번갈아 3회 실행했습니다.
측정 중에는 프로파일러와 빌드를 실행하지 않았습니다.

| 모드 | 변경 전 평균 프레임 시간¹ | 변경 후 평균 프레임 시간¹ | 감소 | p95 전 → 후¹ |
|---|---:|---:|---:|---:|
| 기본 채움 | 11.81 ms | 7.14 ms | 39.5% | 15.46 → 9.84 ms |
| 법선 표시 | 18.30 ms | 13.40 ms | 26.8% | 25.27 → 16.92 ms |
| 와이어프레임 | 14.37 ms | 9.98 ms | 30.5% | 16.30 → 11.46 ms |

¹ 각 실행에서 구한 통계의 3회 중앙값입니다. 평균에는 Update, Render(Clear 포함),
GDI Present가 포함됩니다. 서로 다른 실행의 프레임을 한데 합친 통계가 아닙니다.
전력·온도·백그라운드 작업은 고정하지 않았으며, 실제 보이는 창의 DWM 합성·입력 지연은
대표하지 않습니다. 따라서 위 수치를 사용자 화면의 FPS 보장값으로 해석하지 않습니다.
기존 README의 2025년 FPS 이미지와도 별개의 측정입니다.

### 프로파일로 확인한 원인

**VS CPU Usage:** `VSDiagnostics`로 법선 모드 1200프레임을 수집했습니다.
`.diagsession`의 ETL CPU sample 주소를 당시 EXE/PDB로 해석했습니다.
초기 5초를 제외한 실행 파일 영역의 상위 sample 수는 `binTriangle` 49,075,
`toRasterizerVertex` 18,259, `drawFilledTriangleForTile` 16,206입니다.
이는 instruction-pointer 기반 함수 귀속이며, VS UI의 inclusive call-tree 백분율은 아닙니다.
인라인 함수가 호출한 함수 쪽에 포함될 수 있습니다.

**VTune Hotspots:** software sampling으로 법선·와이어프레임 각각 1200프레임을
변경 전후 수집했습니다. 변경 전 법선 모드에서 타일의
`concurrent_vector<TriangleRef*>::emplace_back`에 8.17 CPU-seconds가 귀속됐고,
`drawFilledTriangleForTile` 5.71, `toRasterizerVertex` 2.19였습니다.
와이어프레임에서는 `drawLineByBresenham` 3.32,
`toRasterizerVertex` 1.26 CPU-seconds가 나왔습니다.
CPU-seconds는 여러 worker의 합이며 프레임 wall time과 다릅니다.
VTune 수집에는 자산 로드·준비 프레임·종료도 포함됩니다.
VTune의 wait/spin 분류와 일부 시스템·TBB symbol 누락을 lock contention의 증거로 단정하지 않았습니다.
자체 EXE/PDB 함수는 해석됐습니다.

변경 후 같은 1200프레임 법선 프로파일에서 bin 삽입은 **8.17 → 1.16 CPU-seconds**,
`drawFilledTriangleForTile`은 **5.71 → 3.18**, `toRasterizerVertex`는 **2.19 → 0.60**으로
줄었습니다. 와이어프레임의 `toRasterizerVertex` 호출은 제거됐습니다.
선 루프는 여전히 중요한 비용이며, 인라인된 작업의 귀속도 바뀌므로 개별 선 함수의 sample 수만으로
속도 개선을 판단하지 않습니다. 위 표의 프로파일러 없는 프레임 시간으로 최종 비교합니다.

### 변경 내용과 유지한 동작

- **정확한 타일 범위:** NDC에 타일 개수를 곱하고 시작 타일을 한 칸 늘리던 방식 대신,
  래스터라이저와 같은 viewport 계산·정수 절삭을 사용합니다. 실제 픽셀 AABB와 겹치는
  타일에만 등록해 동시 삽입과 빈 영역 래스터 준비를 줄였습니다. 부분 타일 해상도도 검사합니다.
- **선 전용 좌표 변환:** 와이어프레임은 화면 좌표만 계산합니다. 사용하지 않는
  법선·UV·월드 위치의 perspective 보간 준비를 생략합니다. 채움만 있을 때는 전체 삼각형
  풀의 와이어프레임 검색도 생략합니다.
- **Bresenham 내부 비용:** 선마다 일정한 색상 변환을 한 번만 수행하고, 중복 인덱스 검사와
  픽셀별 함수 호출을 없앴습니다. 오차 누적·끝점·화면 밖 픽셀 기각 규칙은 유지합니다.
- **Scene TLS 재사용:** 매 프레임 ETS 자체를 파괴하지 않고 각 worker 벡터의 내용만 비워
  등록 정보와 capacity를 재사용합니다. 모든 병렬 작업이 끝난 프레임 경계에서만 초기화합니다.
- oneTBB 타일 병렬 처리, concurrent container, Phong 수식, SSE/AVX dispatch는 유지합니다.
  선·디버그 패스는 픽셀 쓰기 충돌을 막기 위해 여전히 순차 실행합니다.

### 검증과 남은 제한

- Bresenham 400개 선분을 기존 규칙의 기준 구현과 픽셀 단위로 비교합니다.
- 160×128, 137×93에서 경계 포함 200개 삼각형을 모든 타일에 넣는 기준 경로와 비교합니다.
- 기존 수학·로더·조명·FXAA·resize·worker 독립성·실제 자산 테스트도 실행합니다.
- 변경 후 Debug/Release × x64/Win32 네 구성의 앱·테스트 빌드와 회귀 테스트가 모두
  통과했습니다(경고/오류 0개, x64 Debug는 기존 ASan 설정 유지). 이번 검증은 증분 빌드이며,
  직전 유지보수 작업의 clean rebuild와 구분합니다.
- 600프레임 비교의 normal/wire 최종 픽셀 해시는 3회 모두 전후 일치했습니다.
  fill은 변경 전부터 두 종류의 해시가 관측됐고 변경 후 해시는 그중 하나와 일치했습니다.
  전체 장면의 반복 실행 결정성은 이번 수정으로 증명하지 않았으며 원인도 확정하지 않았습니다.
- 실제 표시된 창에서의 수동 시각 회귀, 4K 성능, AVX 미지원 실기기 비교는 하지 않았습니다.
  모든 프레임의 전후 픽셀 일치를 검사한 것도 아닙니다.

## English

On 2026-10-05, alternating three before/after runs on a Core Ultra 9 275HX,
Release x64 at 1280×720 reduced median-of-run-mean frame time from **11.81 to 7.14 ms**
(fill), **18.30 to 13.40 ms** (normals), and **14.37 to 9.98 ms** (wireframe).
Each run uses 60 warmup and 600 measured frames, a fitted fixed camera, fixed time step,
rotating IronMan/teapot, and the real Framework/GDI path in a hidden window.
This is not a headless backend or a visible-window FPS guarantee. Power/thermal conditions
were not controlled. Profilers were not running during these timing comparisons.

VS CPU Usage ETL samples, resolved against the original EXE/PDB, identified binning and
repeated viewport preparation. VTune software Hotspots independently identified concurrent
bin insertion and line rendering. Sample counts are not inclusive call-tree percentages;
aggregated CPU time is not frame wall time. Some vendor/system symbols were unavailable.

Changes tighten bins to the rasterizer's actual pixel bounds, compute only screen positions
for wireframes, hoist per-line color conversion, and retain Scene worker-local capacities.
oneTBB, Phong, SSE/AVX and sequential race-free debug lines remain intact.
Regression tests compare 400 lines and 200 triangles against reference paths, including
partial tiles. Incremental builds and runtime tests passed in all four Debug/Release ×
x64/Win32 configurations with zero warnings/errors. Final normal/wire hashes match before/after in all three runs; fill already
had two baseline hashes, so full-scene determinism is not established. Interactive visual
regression, 4K and non-AVX hardware remain untested.

## 日本語

2026-10-05、Core Ultra 9 275HX・Release x64・1280×720で変更前後を交互に3回実行し、
各回の平均フレーム時間の中央値は、**塗り11.81 → 7.14 ms**、
**法線18.30 → 13.40 ms**、**ワイヤーフレーム14.37 → 9.98 ms**でした。
60フレームの準備後に600フレームを測定し、固定カメラ・固定時間刻みでIronManとteapotを
回転させます。非表示ウィンドウで実際のFramework/GDI経路を使用します。
headless backendでも実画面のFPS保証でもありません。電力・温度条件は固定しておらず、
時間比較中はプロファイラーを動かしていません。

VS CPU UsageのETL sampleを変更前のEXE/PDBで解決し、binningとviewport準備の反復を確認しました。
VTune software Hotspotsでもconcurrent binへの挿入と線描画のコストを確認しました。
sample数はinclusive call-treeの割合ではなく、worker合計CPU時間はフレーム時間とも異なります。
一部のシステム・ベンダーsymbolは未取得です。

実際のピクセル境界に合わせたbin範囲、ワイヤーフレーム用の座標のみの変換、線ごとの色変換、
Sceneのworker-local capacity再利用を適用しました。oneTBB・Phong・SSE/AVXを維持し、
debug線は競合を防ぐため引き続き逐次処理します。400本の線と200個の三角形を基準経路と
比較し、部分タイルも検証します。Debug/Release × x64/Win32の4構成は差分ビルド・回帰テストとも
成功し、警告・エラーは0件です。法線・ワイヤーの最終hashは3回とも変更前後で一致しましたが、
塗りは変更前から2種類のhashがあり、シーン全体の決定性は未確認です。
実画面の手動視覚回帰・4K・AVX非対応実機の検証も残っています。

## Reproduce / 재실행 / 再実行

Run from the repository root in PowerShell:

```powershell
./tests/RunTests.ps1 -Platforms x64 -Configurations Release
./tests/bin/x64/Release/RendererTests.exe . --benchmark 600 1280 720 fill
./tests/bin/x64/Release/RendererTests.exe . --benchmark 600 1280 720 normal
./tests/bin/x64/Release/RendererTests.exe . --benchmark 600 1280 720 wire
```

Modes: `fill`, `normal`, `wire`, `fxaa`, `aabb`. Arguments: frame count, client width,
client height, mode. Timings include Update/Render/Present; the hash covers the final buffer only.

VTune example (run separately from timing comparisons):

```powershell
$exe = (Resolve-Path ./tests/bin/x64/Release/RendererTests.exe).Path
$root = (Get-Location).Path
& 'C:/Program Files (x86)/Intel/oneAPI/vtune/latest/bin64/vtune.exe' `
  -collect hotspots -knob sampling-mode=sw -knob enable-characterization-insights=false `
  -result-dir tests/bin/profiles/vtune-new-normal -- $exe $root --benchmark 1200 1280 720 normal
```

Use a new result directory each time. For VS, use the CPU Usage collector in
`Team Tools/DiagnosticsHub/Collector/VSDiagnostics.exe` with `AgentConfigs/CpuUsageBase.json`,
or open the saved `.diagsession` in Visual Studio. The collected local artifacts are under
`tests/bin/profiles/`: `vs-baseline-normal.diagsession`, `vs-baseline-hotspots.txt`,
`vtune-before-{normal,wire}`, `vtune-after-{normal,wire}`, and `compare-{before,after}-*.txt`.
The original benchmark EXE/PDB are retained in `before/`. These large, machine-specific files
are intentionally git-ignored; they are not part of a fresh checkout.
