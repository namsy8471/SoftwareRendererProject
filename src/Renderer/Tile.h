#pragma once
#include "Renderer/ShaderVertices.h"

struct MeshRenderCommand;

// 매크로는 타입/범위를 잃고 다른 헤더를 오염시킨다. inline constexpr는
// C++17부터 헤더에 안전하게 정의할 수 있으며 디버거에서도 이름이 보인다.
inline constexpr int tile_size = 16; // 타일의 크기 (16x16 픽셀)

// 어떤 메쉬의 몇 번째 삼각형인지를 가리키는 구조체
// TriangleRef는 생성 뒤 읽기 전용이고 각 TBB worker가 자기 pool에서 만든다.
// 과거의 cache-line over-alignment는 false sharing을 막지 않으면서 각 항목에
// padding만 추가했으므로 SIMD 멤버의 자연스러운 16-byte 정렬을 사용한다.
struct TriangleRef
{
    const MeshRenderCommand* sourceCommand = nullptr; // 수명은 해당 프레임의 RenderQueue가 소유한다.
    ShadedVertex sv0;                              // 셰이딩된 버텍스들
	ShadedVertex sv1;                              // 셰이딩된 버텍스들
    ShadedVertex sv2;                              // 셰이딩된 버텍스들
};
