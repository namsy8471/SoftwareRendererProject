---
title: OBJ/MTL 로드, 법선, 텍스처
chapter: 5
lede: 지금까지는 손으로 넣은 삼각형만 그렸습니다. 이제 실제 3D 모델 파일을 읽어 옵니다. 파일 형식은 단순하지만, 현실의 파일은 늘 사양보다 지저분합니다.
demos: [ch05.js]
prev: { title: "4장. 원근 투영, 깊이 버퍼, 클리핑", url: "/ch04/" }
next: { title: "6장. 조명: Lambert에서 Phong까지", url: "/ch06/" }
---

## 5.1 OBJ: 줄마다 한 가지 (`dd02a93`, 2025-07-23)

Wavefront OBJ는 텍스트 파일입니다. 줄의 첫 단어가 그 줄의 종류를 알려 줍니다.

```text
v  0.12 1.50 -0.30      # 정점 위치
vt 0.25 0.75            # 텍스처 좌표
vn 0.0  1.0  0.0        # 법선
f  1/1/1 2/2/2 3/3/3    # 면: 위치/텍스처/법선 번호 (1부터 시작)
usemtl Body             # 이후 면에 쓸 재질 이름 (MTL 파일에 정의)
```

첫 로더는 이 형식을 `std::stringstream`으로 그대로 읽었습니다. 커밋 제목은 "Import OBJ file and paint the vertice only (imcompleted)", 처음에는 정점에 점만 찍었습니다.

```cpp
// dd02a93 ModelLoader.cpp (요약)
while (std::getline(file, line))
{
    std::stringstream ss(line);
    std::string prefix;
    ss >> prefix;
    if (prefix == "v")  { SRMath::vec3 pos; ss >> pos.x >> pos.y >> pos.z; outModel.positions.push_back(pos); }
    else if (prefix == "f")
    {
        std::string face_data;
        while (ss >> face_data)              // "12/7/3" 같은 묶음
        {
            std::stringstream face_ss(face_data);
            std::string token;
            std::getline(face_ss, token, '/');
            outModel.pos_indices.push_back(std::stoi(token) - 1);   // 1부터 → 0부터
            ...
        }
    }
}
```

<div class="note why"><span class="label">이 코드가 느린 이유</span>
줄마다 <code>stringstream</code>을 만들고, 숫자 하나마다 로케일을 확인하는 스트림 변환을 거칩니다. 면 정보는 문자열을 한 번 더 <code>stringstream</code>으로 감싸 쪼갭니다. 작은 모델에서는 문제가 안 되지만 IronMan처럼 수십만 정점인 모델에서는 로딩이 눈에 띄게 느립니다. 2026년 8월의 C++26 현대화(<code>ba95e25</code>)에서 숫자 변환은 <code>std::from_chars</code>로 바뀌었습니다. 할당도 로케일도 없이 문자 범위를 바로 숫자로 바꾸는 함수입니다.</div>

## 5.2 현실의 파일 1: 법선이 없는 면

Utah Teapot OBJ 일부 면에는 `vn` 법선이 없었습니다. 법선이 0 벡터인 채로 조명 계산(6장)에 들어가면 그 부분만 검거나 엉뚱한 밝기가 나옵니다.

해결은 **법선을 직접 만드는 것**입니다. 삼각형의 두 변을 외적하면 면에 수직인 벡터(면 법선)가 나옵니다. 이 값을 그 면의 세 정점에 더해 두고, 모든 면을 돈 뒤 정점마다 정규화하면 주변 면들의 평균 방향, 즉 **부드러운 정점 법선**이 됩니다. 외적의 크기는 삼각형 넓이에 비례하므로, 큰 면이 평균에 더 큰 영향을 주는 것도 자연스럽게 따라옵니다. 넓이가 0인 퇴화 삼각형은 안전한 기본 방향을 씁니다.

<figure>
<svg class="diagram" viewBox="0 0 560 170" role="img" aria-label="면 법선을 평균해 정점 법선 만들기">
  <defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor" style="color:var(--muted)"/></marker></defs>
  <path d="M60,130 L200,90 L340,130" class="box" style="fill:none;stroke-width:2"/>
  <path class="arrow" d="M130,110 L112,58"/><path class="arrow" d="M270,110 L288,58"/>
  <path d="M200,90 L200,30" style="stroke:var(--accent);stroke-width:3;fill:none;marker-end:url(#ah)"/>
  <text x="96" y="50" class="sub">면 법선 A</text><text x="276" y="50" class="sub">면 법선 B</text>
  <text x="208" y="28" style="fill:var(--accent)" font-weight="700">정점 법선 = normalize(A + B)</text>
  <text x="400" y="100" class="sub">외적 크기 = 넓이 × 2</text>
  <text x="400" y="120" class="sub">→ 큰 면일수록 영향이 큼</text>
</svg>
<figcaption>그림 5-1. 두 면이 만나는 정점의 법선. 면 법선을 더해 정규화하면 모서리가 부드럽게 보입니다.</figcaption>
</figure>

## 5.3 현실의 파일 2: 콜론이 들어간 재질 이름

어떤 OBJ는 `usemtl` 이름을 `object-part:material`처럼 `부품:재질` 형태로 썼는데, MTL 파일에는 콜론 뒤의 `material`만 정의되어 있었습니다. 전체 이름으로 찾으면 실패해서 기본 재질(흰색)로 그려졌습니다. 지금 로더는 먼저 전체 이름으로 찾고, 실패하면 **마지막 콜론 뒤**의 이름으로 다시 찾습니다. 사양에 없는 일이지만, 실제 파일을 열려면 필요한 일입니다.

## 5.4 텍스처: 원근 보정 (`eec97e4`, 2025-08-11)

텍스처를 붙이려면 픽셀마다 텍스처 좌표 (u, v)를 구해야 합니다. 3장의 무게중심 좌표로 세 정점의 uv를 보간하면 될 것 같지만, 그렇게 하면 **기울어진 면에서 텍스처가 휘어집니다.** 화면 공간에서는 거리가 균일하지 않기 때문입니다. 가까운 쪽 한 픽셀과 먼 쪽 한 픽셀은 실제 표면에서 다른 길이를 덮습니다.

<div class="demo" data-demo="persp-texture"><p class="demo-fallback">이 데모는 웹 페이지에서 동작합니다.</p></div>

해결은 4장에서 만난 **1/w**입니다. uv 자체는 화면에서 선형이 아니지만 `u/w`, `v/w`, `1/w`는 선형입니다. 그래서 이 세 값을 보간한 다음 나누면 정확한 uv가 나옵니다. 첫 텍스처 커밋은 처음부터 이렇게 했습니다.

```cpp
// eec97e4 Renderer.cpp
final_vertices[j].texcoord_over_w = v_clip.texcoord * one_over_w;     // 정점에서 미리 u/w, v/w
...
SRMath::vec2 uv_over_w_interpolated = v0.texcoord_over_w * w_bary
                                    + v1.texcoord_over_w * u_bary
                                    + v2.texcoord_over_w * v_bary;
SRMath::vec2 uv_interpolated = uv_over_w_interpolated / interpolated_one_over_w;
```

텍스처 읽기는 가장 가까운 텍셀 하나를 고르는 방식(nearest)에, 범위 밖 좌표는 가장자리로 고정(clamp)했습니다.

```cpp
// eec97e4 Texture.cpp
int x = static_cast<int>(std::clamp(u, 0.0f, 1.0f) * (m_width - 1));
int y = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * (m_height - 1));
return pixels_as_int[y * m_width + x];
```

## 5.5 그 뒤에 어떻게 바뀌었나

### (1) 텍스처가 실제로 보이기까지

흥미로운 사실이 하나 있습니다. `eec97e4`에서 텍스처를 읽는 줄은 **주석 처리**되어 있었습니다(`//base_color = material->diffuseTexture->GetPixels(...)`). 원근 보정 uv는 계산했지만 색은 쓰지 않았습니다. 2026년 10월 AI 작업을 시작할 때 코드를 조사해 보니 OBJ의 MTL `map_Kd`(기본 색 텍스처)도 재질에 연결되어 있지 않았습니다. 텍스처가 화면에 실제로 나오기 시작한 것은 **glTF 로더**가 재질의 `baseColorTexture`를 연결하면서부터입니다(23장).

### (2) 샘플링: nearest → bilinear, 밉맵, 감싸기 방식

지금의 `Texture`는 이런 기능을 갖고 있습니다.

| 기능 | 첫 코드 | 지금 |
|---|---|---|
| 필터 | nearest | nearest, **bilinear**(주변 4텍셀 가중 평균) |
| 멀리 있는 면 | 원본 그대로 (지글거림) | **밉맵**: 미리 줄인 이미지 단계에서 읽음 |
| 범위 밖 uv | 가장자리 고정 | 고정, 반복, 거울 반복 (glTF sampler의 wrapS/wrapT) |
| 이미지 해석 | stb_image | 직접 만든 디코더 (PNG/inflate, JPEG, BMP, TGA, DDS) |

데모의 "bilinear"와 "밉맵"을 켜 보세요. 멀리 있는 체크무늬가 지글거리는 현상(앨리어싱)이 밉맵에서 사라집니다.

### (3) 같은 모델을 여러 번: 복제 캐시 (PR #49)

군집 장면은 IronMan 12개와 teapot 10개를 그립니다. 같은 파일을 22번 읽고 해석하면 로딩에 6.0초가 걸렸습니다. 한 번 읽은 모델을 메모리에서 복제하도록 바꾸자 **0.28초**가 됐습니다. 그 뒤 #53에서는 복제조차 하지 않고 인스턴스끼리 메시를 **공유**하게 했습니다.

<div class="note why"><span class="label">이 장의 교훈</span>
파일 형식의 사양보다 <b>실제로 받는 파일</b>이 기준입니다. 법선이 빠진 면, 사양에 없는 이름 규칙, 연결되지 않은 텍스처는 모두 실제 파일을 열어 보고 나서야 드러났습니다. 지금 저장소는 이런 사례마다 회귀 테스트를 두어, 고친 동작이 다시 깨지지 않게 합니다.</div>

## 정리

| | 첫 코드 | 지금 | 바뀐 이유 |
|---|---|---|---|
| OBJ 숫자 해석 | `stringstream` | `std::from_chars` | 할당·로케일 비용 |
| 법선 없는 면 | 0 벡터로 조명 오류 | 면 법선 누적 → 정점 법선 생성 | 실제 파일(teapot) |
| `객체:재질` 이름 | 기본 재질로 떨어짐 | 마지막 콜론 뒤로 다시 찾기 | 실제 파일 |
| uv 보간 | 원근 보정 (u/w, v/w, 1/w) | 같음 | 처음부터 맞게 했음 |
| 텍스처 표시 | 읽는 줄이 주석 처리 | glTF 재질로 연결 | — |
| 샘플링 | nearest, clamp | bilinear, 밉맵, wrap 모드 | 화질 |
| 같은 모델 22개 | 매번 파일 해석 6.0초 | 복제 0.28초 → 공유 (#49, #53) | 로딩 시간 |

## 연습 문제

1. 면 법선을 정규화한 뒤에 더하는 것과, 정규화하지 않고(넓이 가중) 더하는 것은 어떤 모양에서 결과가 크게 다를까요?
2. 데모에서 원근 보정을 끄면 휘어짐은 사각형의 어느 쪽에서 가장 클까요? 왜 그럴까요?
3. 1024×1024 텍스처의 밉맵 단계는 모두 몇 개이고, 전체 메모리는 원본의 몇 배일까요?
