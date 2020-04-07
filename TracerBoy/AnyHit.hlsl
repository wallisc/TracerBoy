#define HLSL
#include "SharedShaderStructs.h"
#include "SharedHitGroup.h"
#include "SharedRaytracing.h"


[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	// TODO: doesn't work with mixed materials
	Material mat = GetMaterial_NonRecursive(MaterialIndex);
	if (IsValidTexture(mat.albedoIndex))
	{
		uint3 indices = GetIndices(PrimitiveIndex());
		float3 barycentrics = GetBarycentrics(attr);
		float2 uv = GetUV(indices, barycentrics);

		float alpha = GetTextureData(mat.albedoIndex, uv).a;
		if (alpha < 0.9f)
		{
			IgnoreHit();
		}
	}


	
}



