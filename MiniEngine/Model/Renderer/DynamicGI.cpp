#include "Core/pch.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Model/Renderer/DynamicGI.h"
#include "Model/Raytracing/RaytracingScene.h"

#include "Output/Model/CompiledShaders/ScreenIndirectIrradianceCS.h"

namespace
{
	enum RootBindings
	{
		kScene,
		kConstants = kScene + RaytracingScene::kNumRootParameters,
		kIndirectIrradiance,

		kNumRootBinding
	};

	RootSignature s_RootSignature;
	ComputePSO s_ScreenIndirectIrradianceCS(L"Screen Indirect Irradiance CS");

	ColorBuffer s_IndirectIrradianceBuffer;

	void EnsureIndirectIrradianceBuffer(uint32_t width, uint32_t height)
	{
		bool hasResource = (s_IndirectIrradianceBuffer.GetResource() != nullptr);
		bool sameSize = (width == s_IndirectIrradianceBuffer.GetWidth() && height == s_IndirectIrradianceBuffer.GetHeight());
		if (!hasResource || !sameSize)
		{
			Graphics::g_CommandManager.IdleGPU();
			s_IndirectIrradianceBuffer.Create(L"Indirect Irradiance Buffer", width, height, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
		}
	}
}

namespace DynamicGI
{
	void Initialize()
	{
		// Root Signgature 생성
		s_RootSignature.Reset(kNumRootBinding, 0);
		RaytracingScene::InitRootParameters(s_RootSignature, kScene);
		s_RootSignature[kConstants].InitAsConstantBuffer(0);
		s_RootSignature[kIndirectIrradiance].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		s_RootSignature.Finalize(L"Screen Indirect Irradiance");

		// Compute PSO 생성
		s_ScreenIndirectIrradianceCS.SetRootSignature(s_RootSignature);
		s_ScreenIndirectIrradianceCS.SetComputeShader(g_pScreenIndirectIrradianceCS, sizeof(g_pScreenIndirectIrradianceCS));
		s_ScreenIndirectIrradianceCS.Finalize();
	}

	void Update(ComputeContext& context, Constants& constants, RaytracingScene& scene)
	{
		ScopedTimer _prof(L"Dynamic GI Update", context);

		context.SetRootSignature(s_RootSignature);
		context.SetPipelineState(s_ScreenIndirectIrradianceCS);

		scene.Bind(context, kScene);
		context.SetDynamicConstantBufferView(kConstants, sizeof(Constants), &constants);
		EnsureIndirectIrradianceBuffer(static_cast<uint32_t>(constants.ViewportSize.x), static_cast<uint32_t>(constants.ViewportSize.y));
		context.TransitionResource(s_IndirectIrradianceBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.SetDynamicDescriptor(kIndirectIrradiance, 0, s_IndirectIrradianceBuffer.GetUAV());
	}
}
