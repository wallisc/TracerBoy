#define HLSL
#include "SharedShaderStructs.h"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	payload.barycentrics = attr.barycentrics;
	payload.objectIndex = InstanceIndex();
}



