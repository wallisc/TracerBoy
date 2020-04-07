#define HLSL
#include "SharedShaderStructs.h"
#include "SharedHitGroup.h"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	uint3 indices = GetIndices(PrimitiveIndex());
	float3 barycentrics = GetBarycentrics(attr);

	payload.uv = GetUV(indices, barycentrics);
	payload.materialIndex = MaterialIndex;
	payload.hitT = RayTCurrent();
	payload.normal = GetNormal(indices, barycentrics);

	// TODO: Only do this if there is a normal map. Should have this specified in some geometry flag
	payload.tangent = GetTangent(indices, barycentrics);
}



