#define HLSL
#include "CompositeAlbedoSharedShaderStructs.h"

Texture2D AlbedoTexture: register(t0);
Texture2D IndirectLightingTexture : register(t1);
Texture2D EmissiveTexture: register(t2);
RWTexture2D<float4> OutputTexture;

[numthreads(COMPOSITE_ALBEDO_THREAD_GROUP_WIDTH, COMPOSITE_ALBEDO_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	float4 albedoAndDiffuseContribution = AlbedoTexture[DTid.xy];
	float3 albedo = albedoAndDiffuseContribution.rgb;
	float diffuseContribution = albedoAndDiffuseContribution.w;
	float specularContribution = 1.0 - diffuseContribution;
	float3 indirectLighting = IndirectLightingTexture[DTid.xy];
	float3 emissive = EmissiveTexture[DTid.xy];
	OutputTexture[DTid.xy] = float4(albedo * indirectLighting * diffuseContribution + indirectLighting * specularContribution + emissive, 1.0) ;
}