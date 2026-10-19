RaytracingAccelerationStructure g_Scene : register(t0);
RWByteAddressBuffer g_Result : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin = float3(0, 0, 0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0;
    ray.TMax = 1e30;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {}

    g_Result.Store(dtid.x * 4, query.CommittedStatus() == COMMITTED_NOTHING ? 1u : 0u);
}
