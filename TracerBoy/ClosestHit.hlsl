#define HLSL
#include "SharedShaderStructs.h"
#include "SharedHitGroup.h"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	float3 barycentrics = GetBarycentrics3(attr.barycentrics);

	GeometryInfo Geometry = GetGeometryInfo(GeometryIndex);
	HitInfo hit = GetHitInfo(Geometry, PrimitiveIndex(), barycentrics);
	payload.uv = hit.uv;
	payload.materialIndex = MaterialIndex;
	payload.hitT = RayTCurrent();
	payload.normal = hit.normal;
	payload.tangent = hit.tangent;
}



