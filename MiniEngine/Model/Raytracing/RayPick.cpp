#include "Model/Raytracing/RayPick.h"
#include "Model/Raytracing/CameraRays.h"
#include "Model/Raytracing/RaytracingScene.h"
#include "Model/Raytracing/SceneHeapTable.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Camera/Camera.h"
#include "Core/Display/Display.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/DepthBuffer.h"
#include "Core/Graphics/Resource/GpuBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"
#include "Core/UI/ImGuiLayer.h"

#include "Output/Model/CompiledShaders/RayPickCS.h"

#include <algorithm>

using namespace Math;
using namespace Graphics;

namespace
{
    // pick grid의 칸 수 한도. 응답이 agent가 읽을 만한 크기에 머물게 한다.
    constexpr uint32_t kMaxPixels = 4096;

    enum RootParameter { kConstants, kScene, kRasterDepth = kScene + RaytracingScene::kNumRootParameters, kPixels, kResults, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"Ray Pick");

    SceneHeapTable s_RasterDepthSRV;
    StructuredBuffer s_Results;
    ReadbackBuffer s_ResultsReadback;

    __declspec(align(16)) struct Constants
    {
        CameraRays Rays;
        XMFLOAT3 CameraForward;
        float NearZ;
        float FarZ;
        uint32_t NumPixels;
    };

    // RayPickCS.hlsl의 Result와 같은 layout이다.
    struct Result
    {
        uint32_t hit;
        float distance;
        float depth;
        XMFLOAT3 position;
        XMFLOAT3 normal;
        uint32_t frontFace;
        uint32_t alphaTest;
        uint32_t instance;
        uint32_t geometry;
        uint32_t primitive;
        uint32_t material;
        XMFLOAT4 baseColor;
        float rasterZ;
        float rasterDepth;
    };
    static_assert(sizeof(Result) == 84, "RayPickCS.hlsl의 Result와 크기가 다르다");

    // grid의 칸은 그리는 frame의 scene 영역에서 정하므로 그때까지 칸 수만 둔다.
    struct Request
    {
        bool grid;
        // grid가 아닐 때 client pixel
        int x;
        int y;
        uint32_t columns;
        uint32_t rows;
        ControlChannel::Reply reply;
    };
    std::vector<Request> s_Requests;

    const char* kUsage = "usage: pick <x> <y> | pick grid <columns>x<rows>";

    std::string FormatFloat(float value)
    {
        char text[32];
        sprintf_s(text, "%.9g", value);
        return text;
    }

    ControlChannel::Fields DescribeVector(const XMFLOAT3& vector)
    {
        return ControlChannel::Fields().Float("x", vector.x).Float("y", vector.y).Float("z", vector.z);
    }

    // 칸 cell은 scene의 [cell * size / count, (cell + 1) * size / count)를 덮는다.
    uint32_t GetCellCenter(uint32_t cell, uint32_t count, uint32_t size)
    {
        return (uint32_t)((2ull * cell + 1) * size / (2ull * count));
    }

    // grid의 한 줄을 칸 순서대로 잇는다. width가 있으면 칸이 줄끼리 세로로 맞도록 오른쪽에 붙여 채운다.
    std::string JoinCells(const std::vector<std::string>& cells, size_t width)
    {
        std::string row;
        for (const std::string& cell : cells)
            row += (row.empty() ? "" : " ") + std::string(width > cell.size() ? width - cell.size() : 0, ' ') + cell;
        return row;
    }

    std::vector<Result> Trace(const std::vector<XMUINT2>& pixels, uint32_t numPixels, const CameraRays& rays, const Camera& camera,
        DepthBuffer& rasterDepth, const RaytracingScene& scene)
    {
        Constants constants;
        constants.Rays = rays;
        XMStoreFloat3(&constants.CameraForward, camera.GetForwardVec());
        constants.NearZ = camera.GetNearClip();
        constants.FarZ = camera.GetFarClip();
        constants.NumPixels = numPixels;

        s_RasterDepthSRV.PointTo(0, rasterDepth, rasterDepth.GetDepthSRV());

        // harness가 명령으로 부를 때만 쏘므로 GPU를 기다려 바로 읽는다.
        ComputeContext& context = ComputeContext::Begin(L"Ray Pick");
        context.SetRootSignature(s_RootSignature);
        context.SetPipelineState(s_PSO);
        context.TransitionResource(rasterDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context.TransitionResource(s_Results, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
        context.SetDynamicSRV(kPixels, sizeof(XMUINT2) * pixels.size(), pixels.data());
        context.SetBufferUAV(kResults, s_Results);
        scene.Bind(context, kScene);
        s_RasterDepthSRV.Bind(context, kRasterDepth);
        context.Dispatch1D(numPixels, 64);
        context.CopyBuffer(s_ResultsReadback, s_Results);
        context.Finish(true);

        std::vector<Result> results(numPixels);
        std::memcpy(results.data(), s_ResultsReadback.Map(), sizeof(Result) * numPixels);
        s_ResultsReadback.Unmap();
        return results;
    }

    ControlChannel::Fields DescribePick(const Result& result, int x, int y, float sampleX, float sampleY)
    {
        ControlChannel::Fields fields;
        fields.Object("pixel", ControlChannel::Fields().Integer("x", x).Integer("y", y))
            .Object("sample", ControlChannel::Fields().Float("x", sampleX).Float("y", sampleY))
            .Flag("hit", result.hit != 0)
            .Flag("alpha_test", result.alphaTest != 0);
        if (result.hit != 0)
        {
            fields.Float("distance", result.distance)
                .Float("depth", result.depth)
                .Object("position", DescribeVector(result.position))
                .Object("normal", DescribeVector(result.normal))
                .Flag("front_face", result.frontFace != 0)
                .Integer("instance", result.instance)
                .Integer("geometry", result.geometry)
                .Integer("primitive", result.primitive)
                .Integer("material", result.material)
                .Object("base_color", ControlChannel::Fields()
                    .Float("r", result.baseColor.x).Float("g", result.baseColor.y).Float("b", result.baseColor.z).Float("a", result.baseColor.w));
        }

        // depth buffer는 far plane인 0으로 지워진다.
        const bool rasterHit = result.rasterZ != 0.0f;
        ControlChannel::Fields raster;
        raster.Flag("hit", rasterHit).Float("z", result.rasterZ);
        if (rasterHit)
            raster.Float("depth", result.rasterDepth);
        return fields.Object("raster", raster);
    }

    ControlChannel::Fields DescribeGrid(const std::vector<Result>& results, const std::vector<XMUINT2>& pixels, uint32_t columns, uint32_t rows, const RECT& scene)
    {
        std::vector<std::string> xs;
        for (uint32_t column = 0; column < columns; ++column)
            xs.push_back(std::to_string(scene.left + (LONG)pixels[column].x));

        std::vector<std::vector<std::string>> instances(rows), materials(rows), distances(rows), positions(rows);
        size_t indexWidth = 1;
        for (uint32_t row = 0; row < rows; ++row)
        {
            for (uint32_t column = 0; column < columns; ++column)
            {
                const Result& result = results[row * columns + column];
                const bool hit = result.hit != 0;
                instances[row].push_back(hit ? std::to_string(result.instance) : "-");
                materials[row].push_back(hit ? std::to_string(result.material) : "-");
                distances[row].push_back(hit ? FormatFloat(result.distance) : "-");
                positions[row].push_back(hit ?
                    FormatFloat(result.position.x) + "," + FormatFloat(result.position.y) + "," + FormatFloat(result.position.z) : "-");
                indexWidth = std::max({ indexWidth, instances[row].back().size(), materials[row].back().size() });
            }
        }

        ControlChannel::Fields rowFields;
        for (uint32_t row = 0; row < rows; ++row)
        {
            rowFields.Object(std::to_string(row), ControlChannel::Fields()
                .Integer("y", scene.top + (LONG)pixels[row * columns].y)
                .Text("instance", JoinCells(instances[row], indexWidth))
                .Text("material", JoinCells(materials[row], indexWidth))
                .Text("distance", JoinCells(distances[row], 0))
                .Text("position", JoinCells(positions[row], 0)));
        }
        return ControlChannel::Fields()
            .Integer("columns", columns)
            .Integer("rows", rows)
            .Text("x", JoinCells(xs, 0))
            .Object("row", rowFields);
    }

    void ReceivePick(const std::string& arguments, const ControlChannel::Reply& reply)
    {
        int x = 0, y = 0;
        uint32_t columns = 0, rows = 0;
        char rest;
        if (sscanf_s(arguments.c_str(), "grid %ux%u %c", &columns, &rows, &rest, 1) == 2)
        {
            if (columns == 0 || rows == 0 || (uint64_t)columns * rows > kMaxPixels)
            {
                reply.Error("a grid has 1 to " + std::to_string(kMaxPixels) + " cells");
                return;
            }
            s_Requests.push_back({ true, 0, 0, columns, rows, reply });
        }
        else if (sscanf_s(arguments.c_str(), "%d %d %c", &x, &y, &rest, 1) == 2)
        {
            s_Requests.push_back({ false, x, y, 0, 0, reply });
        }
        else
        {
            reply.Error(kUsage);
        }
    }
}

void RayPick::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    RaytracingScene::InitRootParameters(s_RootSignature, kScene);
    s_RootSignature[kRasterDepth].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    s_RootSignature[kPixels].InitAsBufferSRV(1);
    s_RootSignature[kResults].InitAsBufferUAV(0);
    s_RootSignature.Finalize(L"Ray Pick");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pRayPickCS, sizeof(g_pRayPickCS));
    s_PSO.Finalize();

    s_RasterDepthSRV.Create(1);
    s_Results.Create(L"Ray Pick Results", kMaxPixels, sizeof(Result));
    s_ResultsReadback.Create(L"Ray Pick Results Readback", kMaxPixels, sizeof(Result));

    ControlChannel::Register("pick", "pick <x> <y> | pick grid <columns>x<rows>: what is seen at a client pixel, by a camera ray traced against the ray tracing scene. "
        "Pixel (x, y) covers [x, x + 1) x [y, y + 1) in client pixels from the top left and must be in the scene, not under the UI. "
        "The ray passes the raster's sample position in it (sample, the pixel's center with TAA off) between the near and far planes, "
        "culling back faces of single-sided materials as the raster does. "
        "distance is along the ray and depth along the camera forward, in world units, as position is. normal is the triangle's, toward the camera. "
        "instance is the mesh in the model, geometry the draw in the mesh, primitive the triangle in the draw and material the glTF material. "
        "base_color is the linear RGBA of the material there. alpha_test is whether alpha testing decided what the ray hit or missed, "
        "as in the Depth Comparison validation: there the raster, which reads the cutout's mip chain where the ray reads mip 0, may see something else. raster has the raster's depth buffer at the pixel: z as stored (reverse Z, 0 is the far plane) "
        "and its depth along the camera forward. grid picks the center pixel of each cell of the scene and gives row by row "
        "the instance, material, distance and position of each cell, - for a miss. A grid has up to 4096 cells", ReceivePick);
}

void RayPick::Shutdown(void)
{
    s_Results.Destroy();
    s_ResultsReadback.Destroy();
}

void RayPick::Render(const Camera& camera, const D3D12_VIEWPORT& rasterViewport, DepthBuffer& rasterDepth, const RaytracingScene& scene)
{
    if (s_Requests.empty())
        return;

    // shader의 raster depth 식과 miss 판정은 reverse Z와 clear 값 0을 전제로 한다.
    ASSERT(camera.GetClearDepth() == 0.0f && rasterDepth.GetClearDepth() == 0.0f);

    const RECT sceneRect = ImGuiLayer::GetSceneViewport();
    const uint32_t width = rasterDepth.GetWidth();
    const uint32_t height = rasterDepth.GetHeight();
    const CameraRays rays = DescribeCameraRays(camera, rasterViewport);
    for (const Request& request : s_Requests)
    {
        // SetDynamicSRV는 16 byte 단위로 읽으므로 pixel 수를 짝수로 채운다.
        std::vector<XMUINT2> pixels;
        uint32_t numPixels = 1;
        if (request.grid)
        {
            numPixels = request.columns * request.rows;
            for (uint32_t row = 0; row < request.rows; ++row)
            {
                for (uint32_t column = 0; column < request.columns; ++column)
                    pixels.push_back(XMUINT2(GetCellCenter(column, request.columns, width), GetCellCenter(row, request.rows, height)));
            }
        }
        else
        {
            const int x = request.x - sceneRect.left;
            const int y = request.y - sceneRect.top;
            if (x < 0 || y < 0 || x >= (int)width || y >= (int)height)
            {
                request.reply.Error("the pixel is not in the scene, which covers x from " + std::to_string(sceneRect.left) + " to " +
                    std::to_string(sceneRect.left + (LONG)width - 1) + " and y from " + std::to_string(sceneRect.top) + " to " +
                    std::to_string(sceneRect.top + (LONG)height - 1) + " in client pixels. The UI covers the rest");
                continue;
            }
            pixels.push_back(XMUINT2((uint32_t)x, (uint32_t)y));
        }
        pixels.resize(Math::AlignUp(pixels.size(), 2));

        const std::vector<Result> results = Trace(pixels, numPixels, rays, camera, rasterDepth, scene);
        ControlChannel::Fields fields;
        fields.Integer("frame", GetFrameCount());
        if (request.grid)
        {
            fields.Object("grid", DescribeGrid(results, pixels, request.columns, request.rows, sceneRect));
        }
        else
        {
            fields.Object("pick", DescribePick(results[0], request.x, request.y,
                (float)sceneRect.left + pixels[0].x + rays.rasterSampleOffset.x, (float)sceneRect.top + pixels[0].y + rays.rasterSampleOffset.y));
        }
        request.reply.Ok(fields);
    }
    s_Requests.clear();
}
