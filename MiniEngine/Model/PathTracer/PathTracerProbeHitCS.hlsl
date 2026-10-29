// Material DDGI Probe Hit의 path tracer. Material마다 따로 compile하는 이유는 PathTracerCS.hlsl의 LAMBERT_MATERIAL에 있다.
#define PROBE_HIT_MATERIAL 1
#include "Model/PathTracer/PathTracerCS.hlsl"
