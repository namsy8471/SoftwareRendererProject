---
title: 선 그리기와 와이어프레임
chapter: 2
lede: 두 점 사이에 선을 긋는 일은 "어느 픽셀을 칠할까"를 정하는 일입니다. 실수 연산 없이 정수 덧셈만으로 그 답을 내는 방법이 Bresenham 알고리즘입니다.
demos: [ch02.js]
prev: { title: "1장. 창 띄우고 픽셀 찍기", url: "/ch01/" }
next: { title: "3장. 삼각형 채우기", url: "/ch03/" }
---

1장에서 `DrawLine`은 빈 함수였습니다. 2025년 7월 15일 커밋 `f673c9d`에서 이 함수가 채워졌고, 두 가지 알고리즘을 메뉴에서 바꿔 쓸 수 있게 했습니다. **DDA**와 **Bresenham**입니다. 둘을 비교하면서 "왜 정수만 쓰는 쪽이 이겼는지"를 보고, 이 선 그리기가 1080p 와이어프레임에서 어떤 병목이 되었는지까지 따라갑니다.

## 2.1 가장 쉬운 방법: DDA

선 위의 점은 `y = y0 + (x - x0) * 기울기`입니다. 더 긴 축으로 한 칸씩 가면서 다른 축은 기울기만큼 더하면 됩니다. 이게 DDA(Digital Differential Analyzer)입니다.

```cpp
// f673c9d Renderer.cpp
void Renderer::drawLineByDDA(int x0, int y0, int x1, int y1, unsigned int color)
{
    const float dy = abs(y1 - y0);
    const float dx = abs(x1 - x0);
    int steps = dx > dy ? dx : dy;

    const float x_inc = dx / (float)steps;
    const float y_inc = dy / (float)steps;
    float x = x0, y = y0;
    for (int i = 0; i <= steps; i++) {
        DrawPixel(x, round(y), color);
        x += x_inc;
        y += y_inc;
    }
}
```

이 코드에는 버그가 하나 있습니다. `dx`, `dy`에 `abs`를 씌웠기 때문에 증가량이 항상 양수입니다. 그래서 **오른쪽 아래로 가는 선만** 맞게 그려지고, 왼쪽이나 위로 가는 선은 반대 방향으로 뻗습니다. 아래 데모에서 "첫 DDA"를 고르고 끝점을 왼쪽 위로 끌어 보세요.

<div class="demo" data-demo="line-step"><p class="demo-fallback">이 데모는 웹 페이지에서 동작합니다.</p></div>

버그를 고쳐도 DDA에는 구조적인 비용이 남습니다. 픽셀마다 실수 덧셈 2번과 `round` 1번, 그리고 실수를 정수로 바꾸는 변환이 있습니다. 실수 누적 오차 때문에 아주 긴 선에서는 끝점이 한 칸 어긋날 수도 있습니다.

## 2.2 정수만 쓰는 방법: Bresenham

Bresenham은 "지금 픽셀이 진짜 선에서 얼마나 벗어났는지"를 **오차(error)** 라는 정수 하나로 들고 다닙니다. 매 칸마다 오차를 보고 x로 갈지, y로 갈지, 둘 다 갈지를 정합니다.

```cpp
// f673c9d Renderer.cpp
void Renderer::drawLineByBresenham(int x0, int y0, int x1, int y1, unsigned int color)
{
    const int dx = abs(x1 - x0);
    const int sx = (x0 < x1) ? 1 : -1;
    const int dy = -abs(y1 - y0);   // y는 음수로 두면 식이 대칭이 된다
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }   // x로 한 칸
        if (e2 <= dx) { err += dx; y0 += sy; }   // y로 한 칸
    }
}
```

<div class="note why"><span class="label">왜 이렇게 했나</span>
<code>err</code>는 "선의 방정식 <code>dy·x − dx·y + c</code>를 지금 픽셀에서 계산한 값"입니다. x로 한 칸 가면 이 값이 정확히 <code>dy</code>만큼, y로 한 칸 가면 <code>dx</code>만큼 변합니다. 그래서 곱셈 없이 덧셈만으로 다음 칸의 값을 알 수 있습니다. 이 "한 칸 이동하면 일정한 양만 더하면 된다"는 생각은 3장의 삼각형 채우기에서 그대로 다시 나옵니다(edge function).</div>

데모에서 "Bresenham"을 고르고 "한 칸씩"을 누르면 매 단계의 `err`, `e2`와 어느 방향으로 갔는지가 보입니다. 방향과 기울기에 상관없이 같은 코드가 동작한다는 점도 확인해 보세요.

## 2.3 와이어프레임: 삼각형 = 선 세 개

이 시점의 `DrawTriangle`은 선 세 개였습니다.

```cpp
void Renderer::DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, unsigned int color)
{
    DrawLine(x0, y0, x1, y1, color);
    DrawLine(x0, y0, x2, y2, color);
    DrawLine(x1, y1, x2, y2, color);
}
```

모델의 모든 삼각형을 이렇게 그리면 와이어프레임이 됩니다. 이 렌더러에는 지금도 세 가지 벤치 모드가 있습니다. **fill**(삼각형 채우기), **wire**(와이어프레임), **normal**(채우기 + 정점마다 법선 선). wire와 normal 모드의 성능은 대부분 이 선 그리기에서 나옵니다.

## 2.4 그 뒤에 어떻게 바뀌었나

### (1) 선도 병렬로: band로 나누기

삼각형 채우기는 10장에서 화면을 타일로 나눠 여러 스레드가 동시에 그리게 됩니다. 선도 같은 일을 하고 싶지만 문제가 있습니다. 긴 선은 화면을 가로지르기 때문에 두 스레드가 같은 픽셀에 동시에 쓸 수 있습니다. 그리고 겹치는 선은 **그린 순서**에 따라 색이 달라지므로, 순서가 바뀌면 화면이 바뀝니다.

지금 코드는 화면을 가로 띠(band)로 나누고, 각 band를 한 스레드가 맡습니다. 한 선이 여러 band를 지나가면 **각 band는 자기 행에 해당하는 부분만** 그립니다. 같은 행은 항상 한 band에만 속하고, band 안에서는 선을 제출 순서대로 그리므로 결과가 순차 실행과 픽셀 단위로 같습니다.

여기서 까다로운 부분은 "선의 중간부터 시작하기"입니다. 끝점을 band 경계로 잘라 새 선을 만들면 기울기가 미세하게 달라져서 원래 선과 다른 픽셀을 칠합니다. 그래서 지금 코드는 원래 선을 k칸 진행했을 때의 위치와 `err` 값을 **나눗셈으로 직접 계산해서** 거기서부터 같은 루프를 돌립니다.

```cpp
// src/Renderer/Rasterizer.cpp (현재)
// 끝점을 band 경계로 다시 자르면 Bresenham의 tie/오차 위상이 달라진다.
// 대신 원래 선의 major-axis step k에서 minor 이동량 floor((k*minor+major/2)/major)를
// 역산한다. 반환 구간의 첫 좌표와 error는 원래 루프를 k번 실행한 상태와 같다.
BresenhamRows restrictBresenhamRows(int startX, int startY, int endX, int endY, int minY, int maxY)
```

### (2) 가장 늦게 끝나는 band가 프레임 시간을 정한다 (PR #6)

1080p 목표를 세우고 처음 쟀을 때 normal 모드는 평균 11.27ms로 기준(p95 ≤ 9ms)을 넘었습니다. band별 시간을 재 보니 원인은 한 band였습니다.

> 고정 64행 band 하나가 3.31ms 중 3.27ms를 차지했다. IronMan의 법선 선이 몇 개 행에 몰려 있었다. (PR #6 커밋 메시지)

band를 **행 수가 아니라 선 개수**가 같아지도록 나누자 결과는 이랬습니다(1920×1080, 600프레임, 6라운드 교대 측정, 평균/p95 ms).

| 모드 | 이전 | PR #6 | 변화 |
|---|---|---|---|
| wire | 7.27 / 8.56 | 5.52 / 6.38 | **−24.1%** |
| normal | 11.27 / 12.69 | 9.32 / 10.61 | **−17.3%** |
| fill | 6.66 / 7.90 | 6.48 / 7.33 | −2.7% |

wire band는 최소 16행, 최대 64개로 바꿨습니다(1080p에서 64행 band 17개였던 것). 모든 모드에서 픽셀 해시가 같았습니다. 즉 화면은 한 픽셀도 바뀌지 않았습니다.

<div class="note why"><span class="label">왜 효과가 컸나</span>
병렬 작업의 시간은 <b>가장 늦게 끝나는 작업</b>이 정합니다. 24개 스레드 중 23개가 일찍 끝나도 한 스레드가 3.27ms를 붙잡고 있으면 프레임은 3.27ms 이상 걸립니다. 작업을 "같은 크기"가 아니라 "같은 비용"으로 나누는 것, 이 원리는 17장(타일 작업 나누기)에서 다시 중요해집니다.</div>

### (3) 시도했지만 버린 것

<div class="note fail"><span class="label">실패 1: band를 더 잘게 (8행, 4행)</span>
band를 잘게 나누면 일이 더 고르게 나뉠 것 같았지만 wire가 8행에서 +6%, 4행에서 +9% <b>느려졌습니다</b>. 기록된 원인은 "한 선을 여러 band가 중복으로 걷게 됨"입니다. band가 얇을수록 긴 선 하나를 더 많은 band가 나눠 맡고, 각 band가 시작 위치를 따로 계산하고 자기 행이 아닌 부분을 건너뛰는 비용이 쌓입니다.</div>

<div class="note fail"><span class="label">실패 2: 분기 없는 Bresenham</span>
<code>if</code> 두 개를 비교 결과를 이용한 산술로 바꾸면 분기 예측 실패가 줄 것이라 기대했습니다. VTune에서 선 루프 시간이 MSVC에서 4.64초 → 5.03초로 오히려 늘었고 GCC에서는 차이가 없었습니다. 선을 따라가는 분기는 패턴이 규칙적이라 CPU가 이미 잘 맞히고 있었고, 분기를 없앤 대신 항상 두 경로를 다 계산하게 된 것으로 보입니다(원인은 추정, 측정값은 기록). 이 결과는 "다시 시도하지 말 것"으로 기록됐습니다.</div>

## 정리

| | 첫 코드 (2025-07) | 지금 | 바뀐 이유 |
|---|---|---|---|
| 알고리즘 | DDA(방향 버그) / Bresenham 선택 | Bresenham | 정수 덧셈만, 오차 누적 없음 |
| 병렬화 | 없음 | 행 band, band 안에서는 제출 순서 | 같은 픽셀 동시 쓰기 방지, 결과 동일 |
| 선 중간에서 시작 | 없음 | k번째 단계의 위치·오차를 역산 | 잘라도 원래 선과 같은 픽셀 |
| band 크기 | 고정 64행 | 선 개수 기준 균형 (#6) | wire −24.1%, normal −17.3% |

## 연습 문제

1. DDA 코드의 버그를 고쳐 보세요. `abs`를 빼면 `steps` 계산은 어떻게 바뀌어야 할까요?
2. 기울기가 1인 선과 기울기가 0.5인 선에서 Bresenham의 `err` 값이 어떻게 변하는지 손으로 5단계 따라가 보세요.
3. 1080p에서 한 band가 3.27ms, 나머지 16개가 0.1ms라면 band 17개를 24스레드로 돌린 프레임은 몇 ms일까요? 선 개수로 균형을 맞춰 모든 band가 같아지면요?
