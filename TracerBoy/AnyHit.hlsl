#define HLSL
#include "SharedShaderStructs.h"
#include "SharedHitGroup.h"
#include "SharedRaytracing.h"


[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	// TODO: doesn't work with mixed materials
	Material mat = GetMaterial_NonRecursive(MaterialIndex);

	float3 barycentrics = GetBarycentrics3(attr.barycentrics);
	GeometryInfo Geometry = GetGeometryInfo(GeometryIndex);
	HitInfo hit = GetHitInfo(Geometry, PrimitiveIndex(), barycentrics);
	if (!IsValidHit(Geometry, hit))
	{
		IgnoreHit();
	}
}



