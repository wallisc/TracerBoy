#include "Tonemap.h"

cbuffer PerFrameCB : register(b0)
{
	PerFrameConstants perFrameConstants;
}

cbuffer ConfigCB : register(b1)
{
	ConfigConstants configConstants;
}

RWTexture2D<float4> OutputTexture : register(u0);
RWTexture2D<float4> AOVNormals : register(u1);
RWTexture2D<float4> AOVWorldPosition : register(u2);
RWTexture2D<float4> AOVSummedLumaSquared : register(u3);
RWTexture2D<float4> AOVCustomOutput : register(u4);

Texture2D LastFrameTexture : register(t0);
RaytracingAccelerationStructure AS : register(t1);
Texture2D EnvironmentMap : register(t4);
StructuredBuffer<float> RandSeedBuffer : register(t5);
StructuredBuffer<Material> MaterialBuffer : register(t6);
StructuredBuffer<TextureData> TextureDataBuffer : register(t7);
Texture2D LuminanceVariance : register(t8);
Texture3D Volume : register(t9);
Texture2D<float4> ImageTextures[] : register(t0, space1);

SamplerState PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);
SamplerState BilinearSamplerClamp : register(s2);

Material GetMaterial_NonRecursive(int MaterialID)
{
	return MaterialBuffer[MaterialID];
}

bool IsValidTexture(uint textureIndex)
{
	return textureIndex != UINT_MAX;
}

float4 GetTextureData(uint textureIndex, float2 uv)
{
	if (configConstants.FlipTextureUVs)
	{
		uv = float2(0, 1) + uv * float2(1, -1);
	}

	float4 data = float4(0.0, 0.0, 0.0, 0.0);
	TextureData textureData = TextureDataBuffer[textureIndex];
	if (textureData.TextureType == IMAGE_TEXTURE_TYPE)
	{
		data = ImageTextures[NonUniformResourceIndex(textureData.DescriptorHeapIndex)].SampleLevel(BilinearSampler, uv, 0);
	}

	if (textureData.TextureFlags & NEEDS_GAMMA_CORRECTION_TEXTURE_FLAG)
	{
		data.rgb = GammaToLinear(data.rgb);
	}

	return data;
}
