---
title: 원근 투영, 깊이 버퍼, 클리핑
chapter: 4
lede: 3D 점을 화면의 2D 점으로 바꾸는 식은 한 줄입니다. 그런데 그 한 줄이 0으로 나누거나 음수로 나누는 순간 렌더러가 터집니다. 투영을 안전하게 만드는 일이 클리핑입니다.
demos: [ch04.js]
prev: { title: "3장. 삼각형 채우기", url: "/ch03/" }
next: { title: "5장. OBJ/MTL 로드, 법선, 텍스처", url: "/ch05/" }
---

## 4.1 정점이 화면에 도착하기까지

모델 파일의 정점 좌표가 화면 픽셀이 되기까지는 행렬 세 개와 나눗셈 한 번, 그리고 화면 크기 변환을 거칩니다. 2025년 7월 24일 커밋 `e896b3b`의 코드가 이 흐름을 그대로 보여 줍니다.

```cpp
// e896b3b Framework.cpp (요약)
SRMath::mat4 modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;  // 모델 → 월드
SRMath::mat4 viewMatrix(1.0f);                                                // 카메라는 원점
SRMath::mat4 projectionMatrix =
    SRMath::perspective(PI / 3.0f, aspectRatio, 0.1f, 100.f);                 // 시야각 60도
...
SRMath::vec4 v0_clip = vp * v0_World;          // 클립 공간 (x, y, z, w)

clip.x /= clip.w;  clip.y /= clip.w;  clip.z /= clip.w;     // 원근 나누기 → NDC (-1..1)
screen.x = (clip.x + 1.0f) * 0.5f * width;                  // 뷰포트 변환
screen.y = (1.0f - clip.y) * 0.5f * height;                 // 화면은 y가 아래로
```

<figure>
<svg class="diagram" viewBox="0 0 720 120" role="img" aria-label="좌표 변환 단계">
  <defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor" style="color:var(--muted)"/></marker></defs>
  <rect class="box" x="0" y="30" width="110" height="56" rx="8"/><text x="55" y="55" text-anchor="middle" font-weight="700">모델 공간</text><text x="55" y="74" text-anchor="middle" class="sub">OBJ의 v</text>
  <rect class="box" x="150" y="30" width="110" height="56" rx="8"/><text x="205" y="55" text-anchor="middle" font-weight="700">월드 공간</text><text x="205" y="74" text-anchor="middle" class="sub">× Model</text>
  <rect class="hot" x="300" y="30" width="120" height="56" rx="8"/><text x="360" y="55" text-anchor="middle" font-weight="700">클립 공간</text><text x="360" y="74" text-anchor="middle" class="sub">× View × Proj</text>
  <rect class="box" x="460" y="30" width="110" height="56" rx="8"/><text x="515" y="55" text-anchor="middle" font-weight="700">NDC</text><text x="515" y="74" text-anchor="middle" class="sub">÷ w</text>
  <rect class="box" x="610" y="30" width="110" height="56" rx="8"/><text x="665" y="55" text-anchor="middle" font-weight="700">화면</text><text x="665" y="74" text-anchor="middle" class="sub">뷰포트</text>
  <path class="arrow" d="M110,58 L147,58"/><path class="arrow" d="M260,58 L297,58"/><path class="arrow" d="M420,58 L457,58"/><path class="arrow" d="M570,58 L607,58"/>
  <text x="360" y="108" text-anchor="middle" class="sub">클리핑은 여기, 나누기 전에 해야 한다</text>
</svg>
<figcaption>그림 4-1. 정점 좌표 변환 단계. w는 카메라에서 정점까지의 깊이에 비례하는 값입니다.</figcaption>
</figure>

원근감은 `÷ w`에서 생깁니다. 멀리 있는 점(w가 큼)은 화면 중심 쪽으로 많이 줄어듭니다. 문제는 w가 **0이거나 음수**일 때입니다. w ≤ 0은 정점이 카메라 옆이나 뒤에 있다는 뜻이고, 이걸로 나누면 좌표가 무한대가 되거나 화면 반대편으로 뒤집힙니다.

## 4.2 첫 시도: 정점 하나를 건너뛰기

`e896b3b`는 w ≤ 0인 정점을 이렇게 처리했습니다.

```cpp
for (int j = 0; j < 3; ++j)
{
    if (clip_coords[j].w <= 0) continue;   // 카메라 뒤의 정점은 건너뜀
    clip_coords[j].x /= clip_coords[j].w;
    ...
    screen_coords[j] = ...;
}
// 그다음 screen_coords 세 개로 삼각형을 그림
```

`continue`는 그 정점의 화면 좌표를 **계산하지 않을 뿐**, 삼각형은 여전히 세 정점으로 그려집니다. 건너뛴 칸에는 이전 프레임이나 초기화되지 않은 값이 남습니다. 카메라에 아주 가까운 물체를 그리면 화면을 몇 배 넘는 거대한 삼각형이 생기고, 그 경계 상자로 깊이 버퍼 밖을 읽고 쓰다가 프로그램이 죽었습니다. README의 "근접 객체의 범위 초과" 사례가 이 문제입니다. 원인은 디버거의 호출 스택과 메모리 감시로 잘못된 인덱스가 만들어지는 경로를 따라가서 찾았습니다.

정점 하나를 버리는 건 답이 아닙니다. **삼각형을 평면으로 잘라서** 카메라 앞쪽 부분만 남겨야 합니다.

## 4.3 Sutherland–Hodgman 클리핑 (`31f3aa5`, `5751375`)

다각형을 평면 하나로 자르는 고전 알고리즘은 변을 하나씩 보면서 네 경우를 처리합니다.

| 이전 정점 | 현재 정점 | 출력 |
|---|---|---|
| 안 | 안 | 현재 정점 |
| 안 | 밖 | 교차점 |
| 밖 | 안 | 교차점, 현재 정점 |
| 밖 | 밖 | 없음 |

삼각형을 한 평면으로 자르면 정점이 3개 또는 4개인 다각형이 나옵니다. 7월 20일에 near 평면(w > 0.1) 하나에 대해 이 알고리즘이 들어갔습니다. 잘린 다각형은 다시 삼각형으로 나눠야 하는데, 첫 코드는 이렇게 나눴습니다.

```cpp
// 5751375: 잘린 다각형을 삼각형으로
for (size_t j = 0; j < clipped_vertices.size() - 2; j++)
{
    clipped_v0 = clipped_vertices[j];
    clipped_v1 = clipped_vertices[j + 1];
    clipped_v2 = clipped_vertices[j + 2];
    ...
    DrawFilledTriangle(...);
}
```

정점 4개(0, 1, 2, 3)면 삼각형 (0,1,2)와 (1,2,3)이 나옵니다. 이건 **띠(strip)** 방식인데, 다각형의 둘레 순서로 나열된 정점에는 맞지 않습니다. 아래 데모에서 "첫 코드 (띠)"를 골라 보세요. 두 삼각형이 겹치는 곳과 빈 곳이 생깁니다.

<div class="demo" data-demo="clip-fan"><p class="demo-fallback">이 데모는 웹 페이지에서 동작합니다.</p></div>

둘레 순서의 볼록 다각형은 **부채꼴(fan)**, 즉 (0,1,2), (0,2,3), (0,3,4)…로 나눠야 빈틈과 겹침이 없습니다. Sutherland–Hodgman의 출력은 항상 볼록 다각형이므로 부채꼴이 언제나 맞습니다.

## 4.4 깊이 버퍼 (`5751375`)

여러 삼각형이 같은 픽셀을 덮으면 가장 가까운 것이 보여야 합니다. 첫 코드는 화면 크기의 `m_depthBuffer`를 두고, 픽셀마다 보간한 깊이를 비교했습니다.

```cpp
float z_interpolated = z0 * w_bary + z1 * u_bary + z2 * v_bary;
int idx = y * m_width + x;
if (z_interpolated < m_depthBuffer[idx])    // 더 가까우면
{
    m_depthBuffer[idx] = z_interpolated;
    DrawPixel(x, y, final_color);
}
```

## 4.5 그 뒤에 어떻게 바뀌었나

### (1) 여섯 평면 모두, 그리고 대부분은 건너뛰기

지금은 near 하나가 아니라 **절두체의 여섯 평면**(좌, 우, 위, 아래, 가까운, 먼) 모두로 자릅니다. 다만 대부분의 삼각형은 화면 안에 완전히 들어 있어서 자를 필요가 없습니다. 그래서 정점마다 6비트 **아웃코드**(어느 평면 밖에 있는지)를 먼저 계산합니다.

```cpp
// src/Renderer/RendererFrontend.cpp (현재)
const auto orOutcode = setup0.outcode | setup1.outcode | setup2.outcode;
if (orOutcode == 0) [[likely]]
{
    // 세 정점이 모두 안쪽: 클리핑 없이 바로 래스터 준비
}
else
{
    if ((setup0.outcode & setup1.outcode & setup2.outcode) != 0)
        continue;     // 세 정점이 모두 같은 평면 밖: 통째로 버림
    clipTriangle(...); // 걸친 삼각형만 자른다
    // 결과 다각형을 부채꼴로 나눔
}
```

OR가 0이면 모두 안, AND가 0이 아니면 모두 같은 쪽 밖입니다. 비트 연산 두 번으로 대부분의 삼각형이 분류됩니다.

### (2) 동차 좌표에서 SIMD로 자르기

평면 판정은 나누기 전의 클립 좌표에서 합니다. 예를 들어 왼쪽 평면은 `x + w ≥ 0`입니다. 이 식은 정점 `(x, y, z, w)`와 평면 벡터 `(1, 0, 0, 1)`의 내적이라서, SSE4.1의 `_mm_dp_ps` 한 명령으로 계산합니다.

```cpp
// src/Renderer/RendererClipping.cpp (현재)
const __m128 leftPlane = _mm_set_ps(1.0f, 0.0f, 0.0f, 1.0f);   //  x + w >= 0
...
__m128 distsPs = _mm_dp_ps(currentPosPs, plane, 0xF1);                 // 현재 정점 거리
distsPs = _mm_add_ps(distsPs, _mm_dp_ps(prevPosPs, plane, 0xF2));      // 직전 정점 거리
int insideMask = _mm_movemask_ps(_mm_cmpge_ps(distsPs, zero));         // 두 판정을 비트 2개로
```

### (3) 힙 할당 없는 ping-pong 버퍼

첫 코드는 삼각형마다 `std::vector`를 몇 개씩 새로 만들었습니다. 지금은 평면 하나를 자를 때마다 입력과 출력 버퍼를 맞바꾸는 **ping-pong** 방식이고, 버퍼는 크기가 고정된 `FixedCapacityVector<ShadedVertex, 12>`입니다. 삼각형을 평면 6개로 자르면 정점은 최대 9개(3 + 6)이므로 12칸이면 충분하고, 힙 할당이 한 번도 일어나지 않습니다.

### (4) 깊이 = 1/w, 큰 값이 가까움

지금의 깊이 버퍼는 NDC의 z가 아니라 **1/w**를 저장하고, **값이 클수록 가깝습니다.** 비어 있는 칸의 값은 `float`의 가장 작은 값입니다.

```cpp
// src/Renderer/Rasterizer.cpp (현재)
float q = t.rv0.oneOverW * a;     // a, b, c = 무게중심 좌표
q += t.rv1.oneOverW * b;
q += t.rv2.oneOverW * c;
const bool visible = q > depthRow[localX] ||
                     (q == depthRow[localX] && t.primitiveId < idRow[localX]);
```

<div class="note why"><span class="label">왜 1/w인가</span>
1/w는 화면 공간에서 <b>정확히 선형</b>입니다. 그래서 무게중심 좌표로 보간해도 오차가 없고, 이 값은 어차피 5장의 원근 보정 텍스처 매핑에 필요합니다. 한 번 계산한 값을 깊이 비교와 원근 보정에 함께 쓰는 셈입니다. 깊이가 같을 때는 3장에서 본 것처럼 삼각형 번호가 작은 쪽이 이겨서, 어떤 스레드 순서로 그려도 결과가 같습니다.</div>

### (5) 유한하지 않은 값은 입구에서 거부

4.2의 사고 이후, 지금 코드는 NaN이나 무한대가 섞인 정점을 래스터까지 보내지 않습니다. 클립 좌표가 유한하지 않으면 삼각형을 건너뛰고(`finiteClip`), 화면 좌표가 2³¹을 넘거나 1/w가 양의 유한수가 아니면 래스터 준비에서 거부합니다. 경계 상자는 항상 화면 안으로 잘립니다.

## 정리

| | 첫 코드 (2025-07) | 지금 | 바뀐 이유 |
|---|---|---|---|
| w ≤ 0 처리 | 정점 건너뛰기 → 쓰레기 좌표, 범위 밖 접근 | 클리핑 + 유한성 검사 | 메모리 오류 |
| 자르는 평면 | near 하나 | 6개, 아웃코드로 대부분 생략 | 정확성 + 비용 |
| 다각형 → 삼각형 | 띠 (겹침과 빈틈) | 부채꼴 | 올바른 분할 |
| 임시 저장소 | 삼각형마다 `std::vector` | 고정 크기 ping-pong 버퍼 | 힙 할당 제거 |
| 깊이 | NDC z, 작은 값이 가까움 | 1/w, 큰 값이 가까움, 같으면 작은 ID | 선형 보간, 원근 보정 재사용, 결정적 결과 |

## 연습 문제

1. 삼각형을 여섯 평면으로 차례로 자르면 정점이 최대 몇 개가 될까요? 왜 12칸 버퍼로 충분할까요?
2. 정점 세 개의 아웃코드가 `000001`, `000010`, `000000`이면 이 삼각형은 버릴 수 있을까요? 클리핑이 필요할까요?
3. w가 1에서 100까지 변할 때 1/w는 어떻게 분포할까요? 가까운 물체와 먼 물체 중 어디에 정밀도가 몰릴까요?
