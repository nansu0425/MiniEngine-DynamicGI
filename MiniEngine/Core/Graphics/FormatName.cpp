#include "Core/pch.h"
#include "Core/Graphics/FormatName.h"

const char* Graphics::GetFormatName(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R32G32_FLOAT: return "R32G32_FLOAT";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
    case DXGI_FORMAT_R8_UNORM: return "R8_UNORM";
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return "R32G32B32A32_TYPELESS";
    case DXGI_FORMAT_R32G32_TYPELESS: return "R32G32_TYPELESS";
    case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
    case DXGI_FORMAT_R8_TYPELESS: return "R8_TYPELESS";
    default: return "Other";
    }
}
