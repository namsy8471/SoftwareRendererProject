---
title: 창 띄우고 픽셀 찍기
chapter: 1
lede: GPU 없이 화면에 무언가를 그리려면 무엇이 필요할까요? 답은 생각보다 단순합니다. 정수 배열 하나와, 그 배열을 화면으로 복사해 주는 함수 하나입니다.
demos: [ch01.js]
---

이 책은 이 저장소의 렌더러가 실제로 걸어온 길을 따라갑니다. 각 장은 **그때 처음 쓴 가장 단순한 코드**로 시작하고, 뒤에서 **그 코드가 왜, 어떻게 바뀌었는지**를 측정값과 함께 봅니다. 1장의 코드는 2025년 7월 8~15일의 첫 커밋들(`6a2d0b7`, `81aad82`, `f673c9d`)에서 왔습니다.

이 장을 마치면 다음을 알게 됩니다.

- 화면이 메모리에서 어떤 모양인지 (`pixels[y * width + x]`)
- Win32에서 CPU가 직접 쓸 수 있는 화면 버퍼를 만드는 법 (`CreateDIBSection`)
- 색의 바이트 순서 때문에 빨강이 파랑으로 나오는 이유
- 더블 버퍼와 게임 루프
- 렌더러 속도를 fps가 아니라 ms로 재야 하는 이유

## 1.1 화면은 정수 배열이다

1920×1080 화면은 픽셀 2,073,600개입니다. 픽셀 하나를 32비트 정수 하나(파랑, 초록, 빨강, 그리고 쓰지 않는 1바이트)로 두면 화면 전체는 `uint32_t` 2,073,600개짜리 배열, 약 8MB입니다. 2차원 화면을 1차원 배열에 담는 방법은 **행 우선(row-major)** 입니다. 맨 윗줄 픽셀을 왼쪽부터 다 놓고, 그다음 줄을 이어 붙입니다. 그래서 `(x, y)` 픽셀은 배열의 `y * width + x` 번째 칸에 있습니다.

<div class="demo" data-demo="pixel-grid"><p class="demo-fallback">이 데모는 <a href="https://namsy8471.github.io/SoftwareRendererProject/ch01/">웹 페이지</a>에서 동작합니다.</p></div>

데모에서 "첫 Render() 실행"을 눌러 보세요. 아래 메모리 띠에서 칠해지는 칸이 **띄엄띄엄 몇 묶음**으로 나타납니다. 사각형은 화면에서는 한 덩어리지만 메모리에서는 행마다 따로 떨어진 조각입니다. 이 사실은 4부에서 다시 중요해집니다. 메모리에서 붙어 있는 것을 연달아 처리해야 캐시가 일을 해 주기 때문입니다(19장, 타일 버퍼).

렌더러가 하는 일은 결국 이 배열의 어느 칸에 어떤 값을 쓸지 정하는 것뿐입니다. 선, 삼각형, 조명, 그림자는 모두 그 값을 정하는 방법입니다.

## 1.2 CPU가 쓸 수 있는 화면 버퍼 만들기

Windows에서 창에 그림을 그리는 가장 기본적인 API는 GDI입니다. GDI에는 `SetPixel(hdc, x, y, color)`라는 함수가 있지만, 픽셀 하나마다 GDI 호출 한 번이 드는 함수라 1080p 화면 200만 픽셀을 이렇게 칠하면 실시간과는 거리가 멉니다. 그래서 첫 코드는 다른 길을 택했습니다. **우리가 직접 쓸 수 있는 메모리를 GDI에게서 받아 오는 것**입니다. 그게 `CreateDIBSection`입니다.

<figure>
<svg class="diagram" viewBox="0 0 720 170" role="img" aria-label="CPU 배열에서 화면까지의 경로">
  <defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor" style="color:var(--muted)"/></marker></defs>
  <rect class="hot" x="10" y="40" width="190" height="80" rx="10"/>
  <text x="105" y="72" text-anchor="middle" font-weight="700">m_pPixelData</text>
  <text x="105" y="95" text-anchor="middle" class="sub">CPU가 직접 쓰는 uint32 배열</text>
  <rect class="box" x="265" y="40" width="190" height="80" rx="10"/>
  <text x="360" y="72" text-anchor="middle" font-weight="700">DIB 섹션 + 메모리 DC</text>
  <text x="360" y="95" text-anchor="middle" class="sub">GDI가 아는 "뒤 버퍼"</text>
  <rect class="box" x="520" y="40" width="190" height="80" rx="10"/>
  <text x="615" y="72" text-anchor="middle" font-weight="700">창 (화면 DC)</text>
  <text x="615" y="95" text-anchor="middle" class="sub">사용자가 보는 곳</text>
  <path class="arrow" d="M200,80 L262,80"/>
  <path class="arrow" d="M455,80 L517,80"/>
  <text x="231" y="66" text-anchor="middle" class="sub">같은 메모리</text>
  <text x="486" y="66" text-anchor="middle" class="sub">BitBlt</text>
  <text x="360" y="150" text-anchor="middle" class="sub">한 프레임 = 배열에 쓰기 → BitBlt 한 번</text>
</svg>
<figcaption>그림 1-1. 첫 렌더러의 출력 경로. 배열과 DIB는 같은 메모리라서 복사는 마지막 BitBlt 한 번뿐입니다.</figcaption>
</figure>

첫 코드의 `Renderer::Initialize`에서 핵심만 추리면 이렇습니다.

```cpp
// 81aad82 Renderer.cpp (2025-07-11)
m_hMemDC = CreateCompatibleDC(hScreenDC);           // 화면과 호환되는 메모리 DC

BITMAPINFO bmi;
ZeroMemory(&bmi, sizeof(BITMAPINFO));
bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
bmi.bmiHeader.biWidth = m_width;
bmi.bmiHeader.biHeight = -m_height; // 중요: 높이를 음수로 설정! [y * width + x]
bmi.bmiHeader.biPlanes = 1;
bmi.bmiHeader.biBitCount = 32;      // 32비트 컬러
bmi.bmiHeader.biCompression = BI_RGB;

m_hBitmap = CreateDIBSection(m_hMemDC, &bmi, DIB_RGB_COLORS,
    (void**)&m_pPixelData, NULL, 0);                // ← 여기서 배열 주소를 받는다
m_hOldBitmap = (HBITMAP)SelectObject(m_hMemDC, m_hBitmap);
```

`biHeight`를 **음수**로 준 이유가 주석에 있습니다. BMP 형식은 원래 맨 아랫줄부터 저장합니다(bottom-up). 높이를 음수로 주면 맨 윗줄부터(top-down) 저장해서, 배열의 0번 칸이 화면 왼쪽 위가 됩니다. 그래야 `y * width + x`가 그대로 성립합니다.

픽셀 하나를 찍는 함수는 이렇게 생겼습니다.

```cpp
void Renderer::DrawPixel(int x, int y, unsigned int color)
{
    // 화면 밖을 침범해서 메모리를 오염시키는 것을 방지합니다.
    if (x < 0 || x >= m_width || y < 0 || y >= m_height)
        return;

    // Color exchange to RGB from BGR
    unsigned char r = (color >> 16) & 0xFF;
    unsigned char g = (color >> 8) & 0xFF;
    unsigned char b = (color >> 0) & 0xFF;
    unsigned int newColor = (r << 0) | (g << 8) | (b << 16);

    m_pPixelData[y * m_width + x] = newColor;
}
```

경계 검사, 색 변환, 배열 쓰기. 이 세 줄이 렌더러의 가장 안쪽 루프입니다. 지금 보면 문제가 두 가지 있습니다. 둘 다 이 장 끝(1.6)에서 다룹니다.

## 1.3 빨강이 파랑으로 나오는 이유

`81aad82`의 커밋 제목은 "Making BGR to RGB transform"입니다. 이 커밋이 왜 필요했는지 직접 보세요. 변환을 끄고 R 슬라이더를 움직이면 됩니다.

<div class="demo" data-demo="bgr"><p class="demo-fallback">이 데모는 웹 페이지에서 동작합니다.</p></div>

원인은 두 규칙이 서로 반대이기 때문입니다.

| | 32비트 값의 모양 | 메모리 바이트 (리틀 엔디안) |
|---|---|---|
| Win32 `RGB(r, g, b)` 매크로 | `0x00BBGGRR` | `RR GG BB 00` |
| 32비트 DIB가 픽셀을 읽는 방식 | `0x00RRGGBB` | `BB GG RR 00` |

`RGB(255, 0, 0)`(빨강)을 그대로 쓰면 메모리 첫 바이트가 `FF`인데, DIB는 첫 바이트를 **파랑**으로 읽습니다. 그래서 빨강이 파랑으로 보입니다. 첫 코드는 픽셀마다 R과 B를 바꿔 넣어서 이 문제를 풀었습니다.

<div class="note why"><span class="label">왜 이렇게 했나</span>
처음엔 익숙한 <code>RGB()</code> 매크로로 색을 만들고 싶었고, 변환을 <code>DrawPixel</code> 한 곳에 넣으면 나머지 코드는 신경 쓸 필요가 없었습니다. 맞는 결정이었지만, 대가는 <b>픽셀마다</b> 시프트 6번과 OR 2번입니다. 1080p에서는 프레임당 200만 번 이상입니다.</div>

## 1.4 게임 루프와 더블 버퍼

일반적인 Windows 프로그램은 메시지가 올 때까지 잠들어 있다가(`GetMessage`), 다시 그려야 할 때만 `WM_PAINT`에서 그립니다. 렌더러는 쉬지 않고 다음 프레임을 그려야 하므로, 첫 코드는 메시지가 없으면 바로 돌아오는 `PeekMessage`로 루프를 만들었습니다.

```cpp
// 6a2d0b7 Framework.cpp (2025-07-08)
while (msg.message != WM_QUIT)
{
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Framework::Update();   // 게임 로직
    Framework::Render();   // Clear() → Render(): 뒤 버퍼(배열)에 그리기

    HDC hdc = GetDC(m_hWnd);
    m_pRenderer->Present(hdc);   // BitBlt: 뒤 버퍼 → 화면
    ReleaseDC(m_hWnd, hdc);
}
```

`Render()`는 배열에만 그리고, 화면에는 `Present()`의 `BitBlt` 한 번으로 옮깁니다. 이것이 **더블 버퍼**입니다. 화면에 직접 그리면 무슨 일이 생기는지 데모로 보세요.

<div class="demo" data-demo="double-buffer"><p class="demo-fallback">이 데모는 웹 페이지에서 동작합니다.</p></div>

단일 버퍼에서는 "지운 검은 화면"과 "반쯤 그린 화면"이 사용자에게 그대로 보여서 깜빡입니다. 더블 버퍼는 다 그린 프레임만 보여 줍니다. 비용은 화면 크기만큼의 메모리 하나와 프레임당 복사 한 번입니다.

## 1.5 속도 재기: fps 카운터

`f673c9d`(2025-07-15)는 창 제목에 평균 fps를 띄우는 카운터를 넣었습니다. 1초 동안 몇 프레임을 그렸는지 세는 방식입니다.

```cpp
// f673c9d PerformanceAnalyzer.cpp
QueryPerformanceCounter(&currentTime);
float deltaTime = float(currentTime.QuadPart - m_prevTime.QuadPart) / m_frequency.QuadPart;
m_prevTime = currentTime;

m_frameCount++;
m_elapsedTime += deltaTime;
if (m_elapsedTime > 1.f)          // 1초마다
{
    m_avgfps = m_frameCount;      // 그동안 그린 프레임 수 = fps
    m_frameCount = 0;
    m_elapsedTime -= 1.f;
}
```

`QueryPerformanceCounter`는 마이크로초보다 정밀한 타이머라서 측정 도구로는 충분합니다. 문제는 **fps라는 단위**입니다. 최적화는 "어떤 일을 몇 ms 줄였다"로 일어나는데, 같은 1ms를 줄여도 fps 변화는 처음 속도에 따라 크게 다릅니다.

<div class="demo" data-demo="fps-ms"><p class="demo-fallback">이 데모는 웹 페이지에서 동작합니다.</p></div>

30fps에서 1ms를 줄이면 약 1fps가 오르지만, 300fps에서 같은 1ms를 줄이면 약 130fps가 오릅니다. fps로 비교하면 뒤쪽 최적화가 훨씬 대단해 보이는 착시가 생깁니다. 또 1초 평균은 가끔 튀는 느린 프레임을 숨깁니다. 그래서 이 프로젝트는 나중에(3부) 기준을 이렇게 바꿉니다.

- 단위는 **프레임 시간(ms)**. 목표 100fps = 프레임당 **10ms**.
- 평균만 보지 않고 **p95**(느린 쪽 5% 경계)를 봅니다. 기준은 p95 ≤ 9ms입니다.
- 변경 전과 후를 같은 PC에서 번갈아 재고, 노이즈(약 1.9%)보다 크게 줄었을 때만 "빨라졌다"고 말합니다(12장).

## 1.6 그 뒤에 어떻게 바뀌었나

1장의 코드는 화면에 빨간 사각형을 띄우기에는 충분했습니다. 하지만 이 코드에서 지금의 코드(`main`, 2026-11)까지 오면서 네 군데가 바뀌었습니다.

### (1) GDI 객체 누수: RAII로 감싸기

첫 `Initialize`에는 지금 보면 눈에 띄는 버그가 하나 있었습니다.

```cpp
m_hBitmap = CreateCompatibleBitmap(hScreenDC, m_width, m_height);   // ① 비트맵을 만들고
m_hBitmap = CreateDIBSection(m_hMemDC, &bmi, ...);                  // ② 바로 덮어씀 → ①은 영영 해제되지 않음
```

창 크기를 바꿀 때마다(`OnResize` → `Initialize`) 화면 크기 비트맵 하나가 새어 나갔습니다. 이 줄은 `91c6c62`("Optimize Renderer")에서 지워졌습니다. 더 근본적인 문제는 `Shutdown()`을 직접 불러야 해제되는 구조라서, 중간에 실패하면 무엇을 해제해야 할지 경로마다 따로 챙겨야 했다는 점입니다. 지금 코드는 GDI 객체를 `GdiBackBuffer`라는 RAII 클래스가 소유하고, 소멸자와 `Reset()`이 만든 역순으로 해제합니다(`ba95e25`, 2026-08 "C++26 현대화"에서 이동 전용 RAII로 바꾸고, `2a280ce`에서 `src/Platform/`으로 분리).

```cpp
// src/Platform/GdiBackBuffer.cpp (현재)
void *rawPixels = nullptr;
m_bitmap = CreateDIBSection(m_memoryDc, &bitmapInfo, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
if (!m_bitmap)
{
    Reset();          // 지금까지 만든 것만 역순으로 해제
    return false;
}
```

### (2) 픽셀마다 하던 색 변환: 한 번에, 처음부터 BGR로

`RGB()`로 만들고 픽셀마다 뒤집는 대신, 지금은 셰이딩 결과(0~1 실수 3개)를 정수로 바꾸는 순간에 **처음부터 DIB 순서로** 만듭니다. 그것도 SSE 명령으로 세 채널을 한꺼번에 처리합니다.

```cpp
// src/Renderer/Rasterizer.cpp packColor (현재)
const __m128 clamped = _mm_max_ps(_mm_min_ps(c.m128, _mm_set1_ps(1.f)), _mm_setzero_ps());
const __m128i channels = _mm_and_si128(_mm_cvttps_epi32(_mm_mul_ps(clamped, _mm_set1_ps(255.f))),
                                       _mm_castps_si128(finite));
// [r, g, b, w] → [b, g, r, w]로 바꿔 바이트로 줄이면 하위 3바이트가 b | g << 8 | r << 16이다.
const __m128i bgr = _mm_shuffle_epi32(channels, _MM_SHUFFLE(3, 0, 1, 2));
```

R과 B를 바꾸는 일이 시프트 6번에서 셔플 명령 **한 개**로 줄었고, NaN 같은 이상한 값도 같은 명령 묶음 안에서 0으로 걸러집니다. SIMD는 9장과 18장에서 자세히 다룹니다.

### (3) 픽셀마다 하던 경계 검사: 타일이 대신 보장

`DrawPixel`은 매번 "화면 안인가?"를 물었습니다. 지금 렌더러는 화면을 16×16 **타일**로 나누고, 삼각형을 그리기 전에 그 타일 범위로 잘라 둡니다(10장). 그래서 가장 안쪽 루프에서는 경계 검사가 필요 없습니다. 검사를 지운 게 아니라, **한 번만 하면 되는 곳으로 옮긴** 것입니다.

### (4) 화면 지우기: GDI FillRect → 병렬 → 타일 안에서

첫 `Clear()`는 GDI의 `FillRect`로 화면 전체를 칠했습니다. 이 일이 얼마나 비싼지는 나중에 측정으로 드러났습니다.

| 시점 | 지우는 방식 | 측정 |
|---|---|---|
| 첫 코드 | `FillRect`로 색만 | 측정 없음 |
| AI 작업 전 | 색 + 깊이 + 삼각형 ID를 메인 스레드 혼자 | 1280×720에서 14.7MB, 프레임당 약 0.66ms (VTune) |
| `RenderTarget::Clear` | 16행씩 나눠 여러 스레드가 병렬로 | |
| PR #17 | 화면 전체 지우기 단계를 없애고 **타일을 그리기 직전에 그 타일만** 지움 | fill −3.1%, normal −4.1% |
| PR #27 | 화면 크기 삼각형 ID 면을 없애고 타일 버퍼 안에서만 씀. 픽셀당 16 → 8바이트 | fill **−13.3%**, wire −8.7%, normal −6.8% |

<div class="note why"><span class="label">왜 지우기가 그렇게 비쌌나</span>
지우기는 계산이 아니라 <b>메모리 쓰기</b>입니다. 1080p에서 색(4B)+깊이(4B)+ID(8B)를 지우면 프레임마다 약 33MB를 씁니다. 이 양은 코어 하나의 L2 캐시(수 MB)보다 열 배 이상 크고 L3도 거의 다 채웁니다. 그래서 화면 전체를 먼저 지우면 지운 값은 캐시에서 밀려났다가, 그릴 때 메모리에서 다시 읽혀야 합니다. 타일(16×16 = 256픽셀, 4KB)만 그리기 직전에 지우면, 지운 데이터가 캐시에 남아 있는 상태에서 바로 그 위에 그립니다. 19장에서 이 이야기를 수치와 함께 끝까지 따라갑니다.</div>

### (5) Present: GdiFlush 한 줄

지금 `Present`에는 `BitBlt` 뒤에 `GdiFlush()`가 붙어 있습니다.

```cpp
// src/Platform/GdiPresenter.cpp (현재)
BitBlt(screenDc, 0, 0, m_surface.width, m_surface.height, m_backBuffer.Get(), 0, 0, SRCCOPY);
// GDI batch가 DIB를 읽는 동안 다음 프레임의 CPU 쓰기가 시작되지 않게 한다.
GdiFlush();
```

GDI는 호출을 모아 두었다가 나중에 실행할 수 있습니다. 그런데 DIB 섹션은 CPU와 GDI가 **같은 메모리**를 씁니다. `BitBlt`가 실제로 끝나기 전에 다음 프레임이 배열을 지우기 시작하면 반쯤 지워진 화면이 복사될 수 있습니다. `CreateDIBSection` 문서는 CPU가 직접 쓰기 전에 `GdiFlush`를 부르라고 요구합니다. 처음 코드는 우연히 문제가 드러나지 않았을 뿐, 규칙을 어기고 있었습니다(`2a280ce`에서 추가).

## 정리

| | 첫 코드 (2025-07) | 지금 (2026-11) | 바뀐 이유 |
|---|---|---|---|
| 버퍼 만들기 | `CreateDIBSection`, 수동 해제, 누수 1개 | `GdiBackBuffer` RAII | 자원 누수, 실패 경로 단순화 |
| 색 순서 | `RGB()` 후 픽셀마다 R↔B 교환 | 셰이딩 끝에서 SSE로 바로 BGR | 픽셀당 연산 제거 |
| 경계 검사 | 픽셀마다 | 삼각형을 타일로 자를 때 한 번 | 안쪽 루프 가볍게 |
| 화면 지우기 | `FillRect` 화면 전체 | 그리기 직전에 타일만, 픽셀당 8B | 메모리 쓰기량 (fill −13.3% 등) |
| Present | `BitBlt` | `BitBlt` + `GdiFlush` | DIB 동기화 규칙 |
| 속도 단위 | 1초 평균 fps | 프레임 시간 ms, p95, A/B | 비교 가능한 숫자 |

## 연습 문제

1. `biHeight`를 양수로 바꾸면 화면이 어떻게 보일까요? `y * width + x` 대신 어떤 식을 써야 같은 화면이 나올까요?
2. 1080p에서 색(4B)+깊이(4B)만 지운다면 프레임당 몇 MB를 쓸까요? 메모리 대역폭이 50GB/s라면 지우기만으로 몇 ms가 걸릴까요?
3. 100fps 목표에서 한 프레임 예산은 10ms입니다. 120fps → 150fps는 몇 ms를 줄인 것일까요? 30fps → 33fps는요?

다음 장에서는 이 배열 위에 처음으로 **선**을 긋습니다. 픽셀 하나에서 선 하나로 가는 데 필요한 것은 나눗셈 없는 정수 덧셈 몇 개입니다(Bresenham).
