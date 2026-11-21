//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "Core/pch.h"
#include "Core/Camera/ShadowCamera.h"

using namespace Math;

void ShadowCamera::UpdateMatrix(
    Vector3 LightDirection, Vector3 ShadowCenter, Vector3 ShadowBounds,
    uint32_t BufferWidth, uint32_t BufferHeight, uint32_t BufferPrecision )
{
    SetLookDirection( LightDirection, Vector3(kZUnitVector) );

    // Converts world units to texel units so we can quantize the camera position to whole texel units
    Vector3 RcpDimensions = Recip(ShadowBounds);
    Vector3 QuantizeScale = Vector3((float)BufferWidth, (float)BufferHeight, (float)((1 << BufferPrecision) - 1)) * RcpDimensions;

    //
    // Recenter the camera at the quantized position
    //

    // Transform to view space
    ShadowCenter = ~GetRotation() * ShadowCenter;
    // Scale to texel units, truncate fractional part, and scale back to world units
    ShadowCenter = Floor( ShadowCenter * QuantizeScale ) / QuantizeScale;
    // Transform back into world space
    ShadowCenter = GetRotation() * ShadowCenter;

    SetPosition( ShadowCenter );

    SetProjMatrix( Matrix4::MakeScale(Vector3(2.0f, 2.0f, 1.0f) * RcpDimensions) );

    Update();

    // Transform from clip space to texture space
    m_ShadowMatrix = Matrix4( AffineTransform( Matrix3::MakeScale( 0.5f, -0.5f, 1.0f ), Vector3(0.5f, 0.5f, 0.0f) ) ) * m_ViewProjMatrix;
}

void ShadowCamera::UpdateMatrixToEnclose(
    Vector3 LightDirection, const AxisAlignedBox& ShadowedBox,
    uint32_t BufferWidth, uint32_t BufferHeight, uint32_t BufferPrecision )
{
    SetLookDirection( LightDirection, Vector3(kZUnitVector) );
    const Quaternion LightToWorld = GetRotation();
    const Quaternion WorldToLight = ~LightToWorld;

    const Vector3 WorldCorners[2] = { ShadowedBox.GetMin(), ShadowedBox.GetMax() };
    AxisAlignedBox LightSpaceBox;
    for (int Corner = 0; Corner < 8; ++Corner)
    {
        const Vector3 WorldCorner(
            WorldCorners[Corner & 1].GetX(),
            WorldCorners[(Corner >> 1) & 1].GetY(),
            WorldCorners[(Corner >> 2) & 1].GetZ());
        LightSpaceBox.AddPoint( WorldToLight * WorldCorner );
    }

    const Vector3 BufferTexels((float)BufferWidth, (float)BufferHeight, (float)((1 << BufferPrecision) - 1));
    const Vector3 QuantizationMarginTexels(2.0f, 2.0f, 2.0f);
    const Vector3 ShadowBounds = LightSpaceBox.GetDimensions() * (BufferTexels + QuantizationMarginTexels) / BufferTexels;

    Vector3 FarPlaneCenter = LightSpaceBox.GetCenter();
    FarPlaneCenter.SetZ( LightSpaceBox.GetMin().GetZ() );

    UpdateMatrix( LightDirection, LightToWorld * FarPlaneCenter, ShadowBounds, BufferWidth, BufferHeight, BufferPrecision );
}
