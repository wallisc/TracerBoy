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
RWTexture2D<float4> JitteredOutputTexture : register(u1);
RWTexture2D<float4> AOVNormals : register(u2);
RWTexture2D<float4> AOVWorldPosition0 : register(u3);
RWTexture2D<float4> AOVWorldPosition1 : register(u4);
RWTexture2D<float4> AOVCustomOutput : register(u5);
RWTexture2D<float4> AOVEmissive : register(u6);

RWByteAddressBuffer StatsBuffer : register(u10);

void OutputGlobalStats(uint NumActiveWaves)
{
	StatsBuffer.Store(0, NumActiveWaves);
}

Texture2D PreviousFrameOutput : register(t0);
RaytracingAccelerationStructure AS : register(t1);

// System Textures
Texture2D BlueNoise0Texture : register(t14);
Texture2D BlueNoise1Texture : register(t15);

// Scene Resources
Texture2D EnvironmentMap : register(t20);
StructuredBuffer<Material> MaterialBuffer : register(t21);
StructuredBuffer<TextureData> TextureDataBuffer : register(t22);

Texture3D Volume : register(t9);
Texture2D<float4> ImageTextures[] : register(t0, space1);
StructuredBuffer<uint2> RayIndexBuffer : register(t12);
ByteAddressBuffer IndirectArgsBuffer: register(t13);

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
	switch(textureData.TextureType)
	{
		case IMAGE_TEXTURE_TYPE:
		{
			data = ImageTextures[NonUniformResourceIndex(textureData.DescriptorHeapIndex)].SampleLevel(BilinearSampler, uv, 0);
			break;
		}
		case CHECKER_TEXTURE_TYPE:
		{
			float2 ScaledUV = uv * float2(textureData.UScale, textureData.VScale);
			data = float4(textureData.CheckerColor1, 1);
			if(((int(ScaledUV.x) + int(ScaledUV.y)) % 2) == 0)
			{
				data = float4(textureData.CheckerColor2, 1);
			}
			break;
		}
		default: break;
	}
	

	if (textureData.TextureFlags & NEEDS_GAMMA_CORRECTION_TEXTURE_FLAG)
	{
		data.rgb = GammaToLinear(data.rgb);
	}

	return data;
}
