// Output마다 따로 compile하는 이유는 PathTracerCS.hlsl의 INDIRECT_IRRADIANCE에 있다.
#define INDIRECT_IRRADIANCE 1
#include "Model/PathTracer/PathTracerCS.hlsl"
