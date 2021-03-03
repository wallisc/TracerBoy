#define HLSL
#include "SharedShaderStructs.h"
#include "SharedHitGroup.h"
#include "SharedRaytracing.h"


[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	// TODO: doesn't work with mixed materials
	Material mat = GetMaterial_NonRecursive(MaterialIndex);

	uint3 indices = GetIndices(PrimitiveIndex());
	float3 barycentrics = GetBarycentrics(attr);
	float2 uv = GetUV(indices, barycentrics);
	if (IsValidTexture(mat.alphaIndex))
	{
		float alpha = GetTextureData(mat.alphaIndex, uv).r;
		if (alpha < 0.9f)
		{
			IgnoreHit();
		}
	}
	else if (IsValidTexture(mat.albedoIndex))
	{
		float alpha = GetTextureData(mat.albedoIndex, uv).a;
		if (alpha < 0.9f)
		{
			IgnoreHit();
		}
	}


	
}



