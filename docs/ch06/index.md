---
title: "조명: Lambert에서 Phong까지"
chapter: 6
lede: 모든 픽셀이 같은 흰색이면 입체감이 없습니다. 빛이 표면에 닿는 각도로 밝기를 정하는 것이 조명입니다. 그리고 1080p에서 조명은 프레임에서 가장 비싼 계산 중 하나가 됩니다.
demos: [ch06.js]
prev: { title: "5장. OBJ/MTL 로드, 법선, 텍스처", url: "/ch05/" }
---

## 6.1 Lambert: 면 하나에 밝기 하나 (`e896b3b`, 2025-07-24)

빛이 표면에 수직으로 내리쬐면 가장 밝고, 비스듬할수록 같은 빛이 넓은 면적에 퍼져서 어두워집니다. 그 비율이 빛 방향과 법선 사이 각의 코사인, 즉 **두 단위 벡터의 내적**입니다. 이게 Lambert(난반사) 모델입니다.

첫 조명 코드는 삼각형마다 면 법선을 구하고, 그 삼각형 전체를 한 색으로 칠했습니다.

```cpp
// e896b3b Framework.cpp
SRMath::vec3 normal_world =
    SRMath::normalize(SRMath::cross(v1_World - v0_World, v2_World - v0_World));
SRMath::vec3 view_dir = SRMath::normalize(v0_World);

if (SRMath::dot(normal_world, view_dir) <= 0)   // 뒷면 컬링
    continue;

float intensity = std::max(0.0f, SRMath::dot(normal_world, light_dir));   // Lambert
SRMath::vec3 final_color_vec = base_color * intensity;
```

같은 코드에 **뒷면 컬링**도 있습니다. 카메라를 등진 면은 어차피 앞면에 가려 보이지 않으므로, 법선과 시선 방향의 내적 부호만 보고 그리기 전에 버립니다. 닫힌 모델이면 삼각형의 약 절반이 여기서 사라집니다.

면마다 밝기가 하나라서 곡면이 각진 다면체로 보입니다(flat shading). 아래 데모의 "면 단위"가 이 모습입니다.

<div class="demo" data-demo="lighting"><p class="demo-fallback">이 데모는 웹 페이지에서 동작합니다.</p></div>

## 6.2 법선을 픽셀마다 보간하기 (`5751375`, 2025-07-28)

4일 뒤 커밋 제목은 "Rendering(Phong shading)"입니다. 여기서 Phong은 두 가지를 뜻할 수 있어서 구분이 필요합니다.

- **Phong 셰이딩**: 정점 법선을 픽셀마다 보간하고, 픽셀마다 조명을 계산하는 방법.
- **Phong 반사 모델**: 주변광 + 난반사 + **정반사(하이라이트)** 로 색을 계산하는 식.

`5751375`가 한 것은 앞쪽입니다. 정점 법선을 무게중심 좌표로 보간해 정규화하고, 픽셀마다 Lambert를 계산했습니다.

```cpp
// 5751375 Renderer.cpp
SRMath::vec3 normal_interpolated =
    SRMath::normalize(n0_view * w_bary + n1_view * u_bary + n2_view * v_bary);
float intensity = std::max(0.0f, SRMath::dot(normal_interpolated, light_dir * -1));
```

5장에서 만든 부드러운 정점 법선이 여기서 빛을 발합니다. 면 경계가 사라지고 곡면이 매끈해집니다.

<div class="note fail"><span class="label">숨어 있던 버그: 법선을 MVP로 변환</span>
이 커밋은 법선을 위치와 같은 <code>mvp</code> 행렬로 변환했습니다(<code>normalize(mvp * vec4(n, 0))</code>). 투영 행렬은 원근을 만들기 위해 공간을 찌그러뜨리므로, 이걸로 변환한 방향은 더 이상 표면에 수직이 아닙니다. 모델에 균일하지 않은 크기 조정이 있어도 같은 문제가 생깁니다. 지금 코드는 법선을 <b>월드 공간</b>에서 다루고, 변환에는 모델 행렬의 <b>역행렬의 전치</b>(<code>inverse_transpose</code>)를 씁니다. 이 행렬은 크기 조정이 있어도 법선이 표면에 수직인 상태를 유지시킵니다.</div>

## 6.3 Phong 반사 모델: 하이라이트

반짝이는 표면에는 빛이 거울처럼 반사되는 밝은 점이 있습니다. 빛 방향을 법선에 대해 반사한 방향이 시선과 가까울수록 밝고, 그 집중도를 `shininess` 지수로 정합니다. 지금 코드의 핵심은 이렇습니다.

```cpp
// src/Renderer/RendererShading.cpp ShadeSurface (현재, 요약)
const float diffuseIntensity = std::max(0.0f, dot(surfaceNormal, lightDir));          // 난반사
const SRMath::vec3 reflectDirection = SRMath::reflect(-1.0f * lightDir, surfaceNormal);
const float specularBase = std::max(0.0f, SRMath::dot(viewDir, reflectDirection));
const float specularFactor = diffuseIntensity > 0.0f && specularBase != 0.0f
                                 ? std::pow(specularBase, std::max(1.0f, material.shininess))
                                 : 0.0f;                                               // 정반사
```

데모에서 "Phong 반사"를 고르고 광택 슬라이더를 움직여 보세요. 지수가 클수록 하이라이트가 작고 날카로워집니다.

## 6.4 그 뒤에 어떻게 바뀌었나: 조명을 싸게

조명 식 자체는 거의 그대로입니다. 바뀐 것은 **몇 번, 어떻게 계산하느냐**입니다. 세 PR이 각자 다른 방향에서 비용을 줄였습니다.

### (1) 결과가 0인 `pow`는 부르지 않기 (PR #6)

VTune으로 normal 모드를 프로파일링하니 `ucrtbase.dll`의 `powf`가 **두 번째로 큰 핫스팟**이었습니다. 그런데 하이라이트가 보이는 픽셀은 화면의 일부입니다. 반사 방향이 시선 반대쪽인 픽셀은 `specularBase`가 +0이고, 지수가 1 이상이면 `pow(+0, y)`는 정확히 +0입니다. 그래서 그 경우에는 호출을 건너뜁니다. 위 코드의 `specularBase != 0.0f` 조건이 그것입니다.

| 측정 (PR #6, 렌더 단계만) | 이전 | 이후 |
|---|---|---|
| normal | 9.28 ms | 9.10 ms |
| fill | 5.35 ms | 5.26 ms |

결과 픽셀은 비트 단위로 같습니다. 데모 아래에 "pow 생략" 비율이 표시되니, 빛을 움직이며 몇 %의 픽셀이 `pow`를 건너뛰는지 보세요.

<div class="note why"><span class="label">왜 "정확히 같다"가 중요한가</span>
이 프로젝트는 최적화의 채택 조건으로 <b>픽셀 해시가 같을 것</b>을 요구합니다. <code>pow</code>를 근사식으로 바꾸면 더 빨라지겠지만 화면이 미세하게 바뀌고, 그러면 "빨라진 것이 최적화 덕인지 다른 그림을 그려서인지" 구분할 수 없습니다. 그래서 IEEE 규칙상 결과가 정확히 +0임이 보장되는 경우에만 건너뜁니다.</div>

### (2) 픽셀마다 한 번만 셰이딩 (PR #24)

첫 코드부터 이어진 순서는 "깊이 테스트를 통과하면 바로 셰이딩해서 색을 쓴다"였습니다. 그런데 나중에 더 가까운 삼각형이 오면 그 색은 덮어써집니다. 계산한 조명이 버려지는 것입니다. IronMan을 1080p로 그리면 **프레임당 312,670픽셀**을 셰이딩했는데, 실제로 보이는 픽셀은 그보다 훨씬 적었습니다.

PR #24는 타일 안에서 순서를 바꿨습니다.

1. 먼저 타일의 모든 삼각형으로 **깊이와 주인(owner) 삼각형만** 정한다.
2. 그다음 각 픽셀을 주인 삼각형으로 **딱 한 번** 셰이딩한다.

셰이딩한 픽셀은 312,670 → **181,480개(−42%)**, PC A/B에서 fill **−6.2%**, normal −4.0%였고 픽셀 해시는 같았습니다. 두 번째 단계에서 무게중심 좌표를 같은 시작점, 같은 식으로 다시 계산하기 때문에 결과가 비트 단위로 같습니다.

### (3) 네 픽셀을 SSE로 한꺼번에 (PR #26)

조명 식은 픽셀마다 같은 연산(정규화, 내적, 반사, max, clamp)입니다. PR #26은 픽셀 네 개를 SSE 레지스터의 네 칸에 하나씩 넣고 한 번에 계산합니다. 정규화의 제곱근과 나눗셈이 픽셀마다 따로 하던 `dpps + sqrtss + divss` 묶음에서, 네 픽셀에 한 번인 `sqrtps / divps`가 됩니다. 덧셈 순서까지 스칼라 코드와 똑같이 맞춰서, 네 칸 모두 스칼라 결과와 **비트 단위로 같음**을 테스트로 확인합니다.

| PC A/B (vs 이전 main) | 이전 | 이후 | 판정 |
|---|---|---|---|
| fill | 3.766 ms | 3.623 ms | **−4.00%**, 채택 |
| normal | 6.847 ms | 6.732 ms | −2.75%, 유의하지 않음 |

18장에서 이 두 PR을 어셈블리와 함께 다시 봅니다. 26장에서는 조명 모델 자체를 물리 기반(PBR)으로 바꾸는 이야기를 합니다.

## 정리

| | 첫 코드 | 지금 | 바뀐 이유 |
|---|---|---|---|
| 조명 단위 | 면 하나에 밝기 하나 | 픽셀마다 | 곡면이 매끈하게 |
| 법선 변환 | MVP로 (틀림) | 월드 공간, 역전치 행렬 | 방향이 표면에 수직이어야 함 |
| 반사 모델 | Lambert | Phong (주변 + 난반사 + 정반사), 이후 PBR | 하이라이트 |
| `pow` | 항상 | 결과가 +0이면 생략 (#6) | VTune 2위 핫스팟 |
| 셰이딩 횟수 | 깊이 통과할 때마다 | 픽셀마다 한 번 (#24, −42%) | 덮어써질 계산 제거 |
| 계산 폭 | 픽셀 하나씩 | SSE로 4픽셀 (#26) | 같은 식을 묶어서 |

## 연습 문제

1. 뒷면 컬링을 `dot(normal, view_dir) <= 0`으로 하면, 카메라가 원점이 아닐 때 `view_dir`은 무엇이어야 할까요?
2. 빛이 정면에서 비칠 때와 옆에서 비칠 때, 데모의 "pow 생략" 비율은 어떻게 달라질까요? 왜 그럴까요?
3. 한 픽셀을 평균 1.7번 덮는 장면에서 "셰이딩 한 번"은 셰이딩 비용을 최대 몇 % 줄일 수 있을까요?

---

**1부를 마치며.** 여기까지가 2025년 7~8월, 렌더러의 뼈대가 만들어진 과정입니다. 화면 버퍼, 선, 삼각형, 투영과 클리핑, 모델과 텍스처, 조명. 2부에서는 이 뼈대를 여러 스레드가 나눠 그릴 수 있는 구조로 바꿉니다.
