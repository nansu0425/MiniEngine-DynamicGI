#pragma once

#include "Core/Math/VectorMath.h"

#include <cstdint>

class Model;
struct MaterialConstants;

struct EmissiveTriangle
{
    uint32_t mesh;
    uint32_t geometry;
    uint32_t primitive;
    Math::XMFLOAT3 positions[3];
    float emission;
};

void CollectEmissiveTriangles(Model& model, const uint8_t* geometryData, const MaterialConstants* materials);
