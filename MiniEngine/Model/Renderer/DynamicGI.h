#pragma once

#include "Core/Graphics/Command/CommandContext.h"

class RaytracingScene;

namespace DynamicGI
{
	struct Constants
	{
		Math::Matrix4 InvViewProj;
		Math::Vector3 CameraPos;
		XMFLOAT2 ViewportOffset;
		XMFLOAT2 ViewportSize;
	};

	void Initialize();
	void Update(ComputeContext& context, Constants& constants, RaytracingScene& scene);
}
