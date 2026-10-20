#pragma once
#include "Platform/Win32Headers.h"

namespace sr::platform
{
// DIB가 선택된 DC는 원래 객체를 복원한 뒤 bitmap, DC 순서로 해제한다.
class GdiBackBuffer
{
  private:
    HDC m_memoryDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_previousBitmap = nullptr;

  public:
    GdiBackBuffer() = default;
    ~GdiBackBuffer() { Reset(); }
    GdiBackBuffer(const GdiBackBuffer &) = delete;
    GdiBackBuffer &operator=(const GdiBackBuffer &) = delete;
    GdiBackBuffer(GdiBackBuffer &&other) noexcept;
    GdiBackBuffer &operator=(GdiBackBuffer &&other) noexcept;

    // 양수 크기의 top-down DIB를 만든다. pixels는 Reset/다음 Create 전까지만 유효하다.
    // 실패 시 pixels는 nullptr이며, 부분 생성된 핸들도 회수한다.
    [[nodiscard]] bool Create(HWND window, int width, int height, unsigned int *&pixels) noexcept;
    void Reset() noexcept;
    [[nodiscard]] HDC Get() const noexcept { return m_memoryDc; }
};

} // namespace sr::platform
