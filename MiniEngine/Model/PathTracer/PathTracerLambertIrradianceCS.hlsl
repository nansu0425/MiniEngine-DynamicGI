// Shading과 Output마다 따로 compile하는 이유는 PathTracerCS.hlsl의 LAMBERT_SHADING과 INDIRECT_IRRADIANCE에 있다.
#define LAMBERT_SHADING 1
#define INDIRECT_IRRADIANCE 1
#include "Model/PathTracer/PathTracerCS.hlsl"
