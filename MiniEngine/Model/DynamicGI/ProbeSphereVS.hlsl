#include "Model/DynamicGI/ProbeSphere.hlsli"

static const float2 kQuadCorners[6] =
{
    float2(-1, -1), float2(1, -1), float2(-1, 1),
    float2(-1, 1), float2(1, -1), float2(1, 1)
};

ProbeQuadVertex main(uint vertexID : SV_VertexID, uint probe : SV_InstanceID)
{
    ProbeQuadVertex vertex;
    vertex.probeCoordinate= GetProbeCoordinate(Grid, probe);
    vertex.probePosition = GetProbePosition(Grid, vertex.probeCoordinate);

    const float3 toCenter = vertex.probePosition - CameraPosition;
    const float centerDistance = length(toCenter);
    
    // camera가 sphere 안으로 들어간 경우 넓이가 0인 triangle로 만들어 pixel을 만들지 않도록 한다    
    if (centerDistance <= Radius)
    {
        vertex.position = float4(0, 0, 0, 1);
        vertex.worldPosition = vertex.probePosition;
        return vertex;
    }

    // quad의 수직, 가로, 세로 world 방향을 구한다
    const float3 axis = toCenter / centerDistance;
    const float3 up = abs(axis.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    const float3 right = normalize(cross(up, axis));
    const float3 down = cross(axis, right);
    const float halfSize = Radius * (centerDistance - Radius) / sqrt(centerDistance * centerDistance - Radius * Radius);

    // quad의 vertex를 world, clip 변환한다
    const float2 corner = kQuadCorners[vertexID];
    vertex.worldPosition = vertex.probePosition - axis * Radius + (right * corner.x + down * corner.y) * halfSize;
    vertex.position = mul(ViewProj, float4(vertex.worldPosition, 1.0));
    
    return vertex;
}
