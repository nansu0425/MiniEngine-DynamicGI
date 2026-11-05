#include "Core/pch.h"
#include "Model/Renderer/DynamicGI.h"

#include "Output/Model/CompiledShaders/PathTracingIndirectIrradianceCS.h"

namespace DynamicGI
{
	RootSignature s_RootSig;
	ComputePSO s_PSO(L"Path Tracing Indirect Irradiance CS");

	void Initialize()
	{

	}

	void Update(ComputeContext& Context)
	{
		ScopedTimer _prof(L"Dynamic GI Update", Context);



		// 3. Irradiance Map UAV 바인딩
		// 4. compute shader Dispatch 2D
	}
}
