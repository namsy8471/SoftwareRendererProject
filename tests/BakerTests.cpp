#include "Graphics/ModelLoader.h"
#include "Renderer/Baker/Baker.h"
#include "Renderer/Baker/DilationFilter.h"
#include "Renderer/Baker/UVSpaceRasterizer.h"
#include "TestHarness.h"
#include "TestSupport.h"

#include <cmath>
#include <vector>

namespace sr::test
{

namespace
{

Mesh createQuadMesh(const SRMath::vec3 &n = {0.f, 0.f, 1.f})
{
    Mesh mesh;
    mesh.vertices = {
        Vertex{.position = {-1.f, -1.f, 0.f}, .texcoord = {0.f, 0.f}, .normal = n},
        Vertex{.position = {1.f, -1.f, 0.f}, .texcoord = {1.f, 0.f}, .normal = n},
        Vertex{.position = {1.f, 1.f, 0.f}, .texcoord = {1.f, 1.f}, .normal = n},
        Vertex{.position = {-1.f, 1.f, 0.f}, .texcoord = {0.f, 1.f}, .normal = n},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.localAABB = ComputeLocalAABB(mesh);
    mesh.ComputeTangents();
    return mesh;
}

} // namespace

void RegisterBakerTests(TestRegistry &registry)
{
    registry.Add("baker.uv_rasterizer", []() {
        const Mesh quad = createQuadMesh();
        const auto uvBuffer = sr::baker::RasterizeMeshUV(quad, 32, 32);

        check(uvBuffer.width == 32 && uvBuffer.height == 32, "UVBuffer size mismatch");
        check(uvBuffer.samples.size() == 32 * 32, "UVBuffer sample count mismatch");

        int validCount = 0;
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                if (uvBuffer.IsValid(x, y))
                    ++validCount;
            }
        }
        // Since quad covers [0, 1]^2 completely, all texels must be valid
        check(validCount == 32 * 32, "All texels in full quad must be covered");

        // Center texel: (16, 16) corresponds to UV ~(0.5, 0.5), position near (0, 0, 0)
        const auto &center = uvBuffer.At(16, 16);
        check(std::abs(center.position.x) < 0.1f, "Center position X near 0");
        check(std::abs(center.position.y) < 0.1f, "Center position Y near 0");
        check(std::abs(center.position.z) < 0.01f, "Center position Z near 0");

        // Normal must be (0, 0, 1)
        check(approximatelyEqual(center.normal.z, 1.0f, 1e-3f), "Center normal must be +Z");

        // Tangent and bitangent must be orthonormal
        const float dotTN = SRMath::dot(center.tangent, center.normal);
        const float dotBN = SRMath::dot(center.bitangent, center.normal);
        const float dotTB = SRMath::dot(center.tangent, center.bitangent);
        check(std::abs(dotTN) < 1e-3f, "Tangent orthogonal to normal");
        check(std::abs(dotBN) < 1e-3f, "Bitangent orthogonal to normal");
        check(std::abs(dotTB) < 1e-3f, "Tangent orthogonal to bitangent");
    });

    registry.Add("baker.dilation_filter", []() {
        constexpr int w = 16;
        constexpr int h = 16;
        std::vector<std::uint32_t> pixels(w * h, 0);
        std::vector<std::uint8_t> mask(w * h, 0);

        // Center 4x4 block: [6..9] x [6..9]
        for (int y = 6; y <= 9; ++y)
        {
            for (int x = 6; x <= 9; ++x)
            {
                pixels[y * w + x] = 0x00FF0000; // Red
                mask[y * w + x] = 1;
            }
        }

        sr::baker::DilateRGB(w, h, pixels, mask, 2);

        // Check that (5, 5) which is distance 1 from the block is now dilated
        check(mask[5 * w + 5] == 1, "Distance 1 texel should be dilated");
        check(pixels[5 * w + 5] == 0x00FF0000, "Distance 1 texel should have color of neighbor");

        // Check that (4, 4) which is distance 2 from the block is now dilated
        check(mask[4 * w + 4] == 1, "Distance 2 texel should be dilated");
        check(pixels[4 * w + 4] == 0x00FF0000, "Distance 2 texel should have color of neighbor");

        // Check that (0, 0) which is distance > 2 remains unmasked
        check(mask[0] == 0, "Distance > 2 texel should remain empty");
        check(pixels[0] == 0, "Distance > 2 texel color should remain 0");
    });

    registry.Add("baker.normal_flat", []() {
        const Mesh lowQuad = createQuadMesh();
        const Mesh highQuad = createQuadMesh();

        sr::baker::BakeOptions options;
        options.width = 32;
        options.height = 32;
        options.bakeNormal = true;
        options.bakeAO = false;
        options.dilationRadius = 0;

        const auto result = sr::baker::Bake(highQuad, lowQuad, options);

        check(result.width == 32 && result.height == 32, "Result dimension mismatch");
        check(result.normalMap.pixels.size() == 32 * 32, "Pixel count mismatch");

        // Flat normal in tangent space should be (128, 128, 255)
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                const std::uint32_t px = result.normalMap.pixels[y * 32 + x];
                const int r = (px >> 16) & 0xFF;
                const int g = (px >> 8) & 0xFF;
                const int b = px & 0xFF;

                check(std::abs(r - 128) <= 1, "R channel must be flat 128");
                check(std::abs(g - 128) <= 1, "G channel must be flat 128");
                check(std::abs(b - 255) <= 1, "B channel must be flat 255");
            }
        }
    });

    registry.Add("baker.normal_perturbed", []() {
        const Mesh lowQuad = createQuadMesh(); // Normal = (0, 0, 1)

        // High poly quad with tilted normal: (1/sqrt(2), 0, 1/sqrt(2))
        const float invSqrt2 = 1.0f / std::sqrt(2.0f);
        const Mesh highQuad = createQuadMesh({invSqrt2, 0.f, invSqrt2});

        sr::baker::BakeOptions options;
        options.width = 32;
        options.height = 32;
        options.bakeNormal = true;
        options.bakeAO = false;
        options.dilationRadius = 0;

        const auto result = sr::baker::Bake(highQuad, lowQuad, options);

        const std::uint32_t centerPx = result.normalMap.pixels[16 * 32 + 16];
        const int r = (centerPx >> 16) & 0xFF;
        const int g = (centerPx >> 8) & 0xFF;
        const int b = centerPx & 0xFF;

        // Tilted along X: nx ~ 0.7071 -> R = round((0.7071 * 0.5 + 0.5) * 255) = round(217.65) = 218
        // ny ~ 0.0 -> G = 128
        // nz ~ 0.7071 -> B = 218
        check(r > 200, "Tilted normal R must be significantly greater than 128");
        check(std::abs(g - 128) <= 2, "Tilted normal G must remain near 128");
        check(b > 200, "Tilted normal B must be significantly greater than 128");
    });

    registry.Add("baker.ao_plane_unoccluded", []() {
        const Mesh lowQuad = createQuadMesh();
        const Mesh highQuad = createQuadMesh();

        sr::baker::BakeOptions options;
        options.width = 32;
        options.height = 32;
        options.bakeNormal = false;
        options.bakeAO = true;
        options.aoSamples = 32;
        options.dilationRadius = 0;

        const auto result = sr::baker::Bake(highQuad, lowQuad, options);

        // Open plane: every hemisphere ray is unoccluded, so AO should be 255 (1.0)
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                const std::uint32_t px = result.aoMap.pixels[y * 32 + x];
                const int gray = px & 0xFF;
                check(gray == 255, "Unoccluded plane AO must be 255");
            }
        }
    });

    registry.Add("baker.ao_corner_occluded", []() {
        // Floor plane in [0, 2] x [0, 2] in XY plane at Z = 0
        Mesh floorMesh;
        floorMesh.vertices = {
            Vertex{.position = {0.f, 0.f, 0.f}, .texcoord = {0.f, 0.f}, .normal = {0.f, 0.f, 1.f}},
            Vertex{.position = {2.f, 0.f, 0.f}, .texcoord = {1.f, 0.f}, .normal = {0.f, 0.f, 1.f}},
            Vertex{.position = {2.f, 2.f, 0.f}, .texcoord = {1.f, 1.f}, .normal = {0.f, 0.f, 1.f}},
            Vertex{.position = {0.f, 2.f, 0.f}, .texcoord = {0.f, 1.f}, .normal = {0.f, 0.f, 1.f}},
        };
        floorMesh.indices = {0, 1, 2, 0, 2, 3};
        floorMesh.localAABB = ComputeLocalAABB(floorMesh);
        floorMesh.ComputeTangents();

        // High poly geometry: Floor PLUS vertical wall at X = 0 (Y in [0, 2], Z in [0, 2])
        Mesh highMesh;
        highMesh.vertices = floorMesh.vertices;
        highMesh.indices = floorMesh.indices;
        const unsigned int baseIdx = static_cast<unsigned int>(highMesh.vertices.size());
        highMesh.vertices.push_back(
            Vertex{.position = {0.f, 0.f, 0.f}, .texcoord = {0.f, 0.f}, .normal = {1.f, 0.f, 0.f}});
        highMesh.vertices.push_back(
            Vertex{.position = {0.f, 2.f, 0.f}, .texcoord = {1.f, 0.f}, .normal = {1.f, 0.f, 0.f}});
        highMesh.vertices.push_back(
            Vertex{.position = {0.f, 2.f, 2.f}, .texcoord = {1.f, 1.f}, .normal = {1.f, 0.f, 0.f}});
        highMesh.vertices.push_back(
            Vertex{.position = {0.f, 0.f, 2.f}, .texcoord = {0.f, 1.f}, .normal = {1.f, 0.f, 0.f}});

        highMesh.indices.push_back(baseIdx + 0);
        highMesh.indices.push_back(baseIdx + 1);
        highMesh.indices.push_back(baseIdx + 2);
        highMesh.indices.push_back(baseIdx + 0);
        highMesh.indices.push_back(baseIdx + 2);
        highMesh.indices.push_back(baseIdx + 3);
        highMesh.localAABB = ComputeLocalAABB(highMesh);
        highMesh.ComputeTangents();

        sr::baker::BakeOptions options;
        options.width = 32;
        options.height = 32;
        options.bakeNormal = false;
        options.bakeAO = true;
        options.aoSamples = 64;
        options.aoMaxDistance = 5.0f;
        options.dilationRadius = 0;

        const auto result = sr::baker::Bake(highMesh, floorMesh, options);

        // Near the corner (x = 1, y = 16) corresponds to X ~ 0.09, close to the wall
        const int nearCornerAO = result.aoMap.pixels[16 * 32 + 1] & 0xFF;
        // Far from the corner (x = 30, y = 16) corresponds to X ~ 1.9, far from the wall
        const int farCornerAO = result.aoMap.pixels[16 * 32 + 30] & 0xFF;

        // Near corner should have significant occlusion (roughly ~50% hemisphere blocked -> ~128)
        check(nearCornerAO < 180, "Near corner AO should be significantly occluded");
        // Far from corner should have high unocclusion (~255)
        check(farCornerAO > 220, "Far from corner AO should be mostly unoccluded");
        check(nearCornerAO < farCornerAO, "Near corner must be darker than far corner");
    });
}

} // namespace sr::test
