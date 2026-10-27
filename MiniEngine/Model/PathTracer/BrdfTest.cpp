#include "Model/PathTracer/BrdfTest.h"
#include "Core/pch.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Resource/GpuBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"
#include "Core/Utility/Utility.h"

#include "Output/Model/CompiledShaders/BrdfTestCS.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

using namespace Graphics;

namespace
{
    // BrdfTestCS.hlsl의 kEvaluate, kQuadrature, kSample 순서다.
    enum Mode { kEvaluate, kQuadrature, kSample, kNumModes };
    const char* kModeNames[] = { "evaluate", "quadrature", "sample" };
    constexpr uint32_t kRecordBytes = 64;
    constexpr uint32_t kGroupSize = 256;
    const uint32_t kResultBytes[] = { 16, 32, 64 };
    // 한 dispatch의 group 수. 명령 하나가 harness의 hang 판정 (frame이 10 s 동안 멈춤)보다 짧게, dispatch 하나가 TDR보다 짧게 끝나도록
    // 나눈다. quadrature는 group 하나가 기록 하나를 수만 번 평가하고, sample은 kGroupSize × samples per thread 번 고른다.
    const uint32_t kGroupsPerDispatch[] = { 4096, 256, 1024 };
    // 기록 수의 상한. 응답과 파일이 harness가 다룰 만한 크기에 머물게 한다.
    constexpr uint32_t kMaxRecords = 1u << 20;

    const char* kUsage = "usage: brdf evaluate <input> <output> | brdf quadrature <theta nodes>x<phi nodes> <input> <output> | "
        "brdf sample <samples per thread> <input> <output>";

    __declspec(align(16)) struct Constants
    {
        uint32_t Mode;
        uint32_t NumRecords;
        uint32_t FirstRecord;
        uint32_t ThetaNodes;
        uint32_t PhiNodes;
        uint32_t SamplesPerThread;
    };

    enum RootParameter { kConstants, kRecords, kResults, kNumRootParameters };
    RootSignature s_RootSignature;
    ComputePSO s_PSO(L"BRDF Test");

    bool ReadBytes(const std::string& path, std::vector<char>& bytes)
    {
        std::ifstream file(Utility::ToWide(path), std::ios::binary);
        if (!file)
            return false;
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !file.bad();
    }

    bool WriteBytes(const std::string& path, const void* bytes, size_t size)
    {
        std::ofstream file(Utility::ToWide(path), std::ios::binary);
        file.write((const char*)bytes, size);
        return file.good();
    }

    void Run(Constants constants, const std::string& inputPath, const std::string& outputPath, const ControlChannel::Reply& reply)
    {
        std::vector<char> records;
        if (!ReadBytes(inputPath, records))
        {
            reply.Error("cannot read " + inputPath);
            return;
        }
        if (records.empty() || records.size() % kRecordBytes != 0 || records.size() / kRecordBytes > kMaxRecords)
        {
            reply.Error(inputPath + " has " + std::to_string(records.size()) + " bytes, not 1 to " + std::to_string(kMaxRecords) +
                " records of " + std::to_string(kRecordBytes) + " bytes");
            return;
        }
        const Mode mode = (Mode)constants.Mode;
        const uint32_t numRecords = (uint32_t)(records.size() / kRecordBytes);
        const uint32_t resultBytes = numRecords * kResultBytes[mode];
        constants.NumRecords = numRecords;

        // harness가 명령으로 부를 때만 돌리므로 buffer를 그때 만들고 GPU를 기다려 바로 읽는다.
        ByteAddressBuffer recordBuffer, resultBuffer;
        recordBuffer.Create(L"BRDF Test Records", (uint32_t)records.size() / 4, 4, records.data());
        resultBuffer.Create(L"BRDF Test Results", resultBytes / 4, 4);
        ReadbackBuffer readback;
        readback.Create(L"BRDF Test Readback", resultBytes / 4, 4);

        const uint32_t recordsPerGroup = mode == kEvaluate ? kGroupSize : 1;
        const uint32_t numGroups = (numRecords + recordsPerGroup - 1) / recordsPerGroup;
        for (uint32_t firstGroup = 0; firstGroup < numGroups; firstGroup += kGroupsPerDispatch[mode])
        {
            constants.FirstRecord = firstGroup * recordsPerGroup;
            ComputeContext& context = ComputeContext::Begin(L"BRDF Test");
            context.SetRootSignature(s_RootSignature);
            context.SetPipelineState(s_PSO);
            context.TransitionResource(recordBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            context.TransitionResource(resultBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
            context.SetDynamicConstantBufferView(kConstants, sizeof(constants), &constants);
            context.SetBufferSRV(kRecords, recordBuffer);
            context.SetBufferUAV(kResults, resultBuffer);
            context.Dispatch(std::min(kGroupsPerDispatch[mode], numGroups - firstGroup));
            context.Finish(true);
        }

        ComputeContext& context = ComputeContext::Begin(L"BRDF Test Readback");
        context.CopyBuffer(readback, resultBuffer);
        context.Finish(true);
        const bool written = WriteBytes(outputPath, readback.Map(), resultBytes);
        readback.Unmap();
        if (!written)
        {
            reply.Error("cannot write " + outputPath);
            return;
        }
        reply.Ok(ControlChannel::Fields().Integer("records", numRecords).Text("output", outputPath));
    }

    void ReceiveBrdf(const std::string& arguments, const ControlChannel::Reply& reply)
    {
        char name[16] = {};
        char input[MAX_PATH] = {}, output[MAX_PATH] = {};
        char rest;
        Constants constants = {};
        if (sscanf_s(arguments.c_str(), "%15s", name, (unsigned)sizeof(name)) != 1)
        {
            reply.Error(kUsage);
            return;
        }
        const char* parameters = arguments.c_str() + strlen(name);
        int parsed = 0;
        if (strcmp(name, kModeNames[kEvaluate]) == 0)
        {
            constants.Mode = kEvaluate;
            parsed = sscanf_s(parameters, " %259s %259s %c", input, (unsigned)sizeof(input), output, (unsigned)sizeof(output), &rest, 1) == 2;
        }
        else if (strcmp(name, kModeNames[kQuadrature]) == 0)
        {
            constants.Mode = kQuadrature;
            parsed = sscanf_s(parameters, " %ux%u %259s %259s %c", &constants.ThetaNodes, &constants.PhiNodes, input, (unsigned)sizeof(input),
                output, (unsigned)sizeof(output), &rest, 1) == 4 && constants.ThetaNodes > 0 && constants.PhiNodes > 0;
        }
        else if (strcmp(name, kModeNames[kSample]) == 0)
        {
            constants.Mode = kSample;
            parsed = sscanf_s(parameters, " %u %259s %259s %c", &constants.SamplesPerThread, input, (unsigned)sizeof(input),
                output, (unsigned)sizeof(output), &rest, 1) == 3 && constants.SamplesPerThread > 0;
        }
        if (!parsed)
        {
            reply.Error(kUsage);
            return;
        }
        Run(constants, input, output, reply);
    }
}

void BrdfTest::Initialize(void)
{
    s_RootSignature.Reset(kNumRootParameters);
    s_RootSignature[kConstants].InitAsConstantBuffer(0);
    s_RootSignature[kRecords].InitAsBufferSRV(0);
    s_RootSignature[kResults].InitAsBufferUAV(0);
    s_RootSignature.Finalize(L"BRDF Test");

    s_PSO.SetRootSignature(s_RootSignature);
    s_PSO.SetComputeShader(g_pBrdfTestCS, sizeof(g_pBrdfTestCS));
    s_PSO.Finalize();

    ControlChannel::Register("brdf", std::string(kUsage) + ". Calls the glTF BRDF of Core/Shaders/GltfBrdf.hlsli on the GPU for each "
        "64-byte record of the input file, little-endian floats unless named, and writes the results to the output file in record order. "
        "A record starts with base color rgb, metallic, unit normal xyz and roughness. "
        "evaluate: then view xyz, a float, light xyz and a float, all unit vectors pointing away from the surface; the result is f rgb and "
        "a float (16 bytes). quadrature: then a unit tangent xyz, a uint, view xyz and a uint; it integrates f cos and f cos g over the "
        "hemisphere, g = (1 + tangent . light)^2, by the midpoint rule over the half vector, with the given nodes per half-angle interval "
        "and in azimuth. The result is the f cos integral rgb, the number of evaluations that were negative or not finite, the f cos g "
        "integral rgb and the number of evaluations (32 bytes). sample: then a unit tangent xyz, a uint seed, view xyz and a uint chunk; each "
        "of 256 threads draws the given number of directions with the BRDF's sampling, chunk and thread picking the random numbers. "
        "The result is the sums of weight rgb (f cos / pdf), invalid f, weight squared rgb, invalid weights, weight g rgb, the largest "
        "|length - 1| of the directions, (weight g) squared rgb and a float (64 bytes). Invalid weights add nothing",
        ReceiveBrdf);
}
