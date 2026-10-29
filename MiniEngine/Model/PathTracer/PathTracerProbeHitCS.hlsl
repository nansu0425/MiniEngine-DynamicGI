// Shading DDGI Probe Hit의 path tracer. Shading마다 따로 compile하는 이유는 PathTracerCS.hlsl의 LAMBERT_SHADING에 있다.
#define PROBE_HIT_SHADING 1
#include "Model/PathTracer/PathTracerCS.hlsl"
