#define HLSL
#include "DenoiserSharedShaderStructs.h"
#include "Tonemap.h"

Texture2D InputTexture : register(t0);
Texture2D AOVNormals : register(t1);
Texture2D AOVIntersectPosition : register(t2);
Texture2D UndenoisedTexture : register(t4);

RWTexture2D<float4> OutputTexture;

cbuffer DenoiserCB
{
	DenoiserConstants Constants;
}

#define KERNEL_WIDTH 5

float3 GetNormal(int2 coord)
{
	float4 normalData = AOVNormals[coord];
	if (normalData.w == 0.0f) return float3(0.0f, 0.0f, 0.0f);

	return normalize(normalData.xyz / normalData.w);
}

float CalculateWeight(
	float centerLuma,
	float centerLuminanceVariance,
	float3 centerNormal, 
	float3 centerIntersectPosition, 
	float distanceToNeighborPixel, 
	int2 coord, 
	int2 offset)
{
	float luma = ColorToLuma(UndenoisedTexture[coord]);
	float centerVarianceSqrt = sqrt(centerLuminanceVariance);
	float lumaWeight = exp(-abs(luma - centerLuma) / (max(Constants.LumaWeightingMultiplier * centerVarianceSqrt, EPSILON)));

	float3 normal = GetNormal(coord);
	float normalWeightExponential = 128.0f;
	float normalWeight = pow(max(0.0f, dot(centerNormal, normal)), Constants.NormalWeightingExponential);

	float positionWeightMultiplier = 1.0f;
	float3 intersectedPosition = AOVIntersectPosition[coord].xyz;
	float distance = length(intersectedPosition - centerIntersectPosition);
	float positionWeight = exp(-distance / (Constants.IntersectionPositionWeightingMultiplier * abs(dot(offset, float2(distanceToNeighborPixel, distanceToNeighborPixel))) + EPSILON));

	float weights[3] = { 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };
	return lumaWeight * positionWeight * normalWeight* weights[abs(offset.x / int(Constants.OffsetMultiplier))] * weights[abs(offset.y / int(Constants.OffsetMultiplier))];
}

bool ValidNormal(float3 normal)
{
	return normal.x != 0.0f || normal.y != 0.0f || normal.z != 0.0f;
}

#define MEDIAN_KERNEL_SIZE 3
struct LumaAndCoord
{
	float Luma;
	int2 Coord;
};

uint2 GetMedianCoord(uint2 DTid)
{
	int index = 0;
	const uint cArraySize = MEDIAN_KERNEL_SIZE * MEDIAN_KERNEL_SIZE;
	LumaAndCoord lumaAndCoordArray[cArraySize];
	for (int x = -MEDIAN_KERNEL_SIZE / 2; x <= MEDIAN_KERNEL_SIZE / 2; x++)
	{
		for (int y = -MEDIAN_KERNEL_SIZE / 2; y <= MEDIAN_KERNEL_SIZE / 2; y++)
		{
			int2 Coord = int2(DTid) + int2(x, y);
			lumaAndCoordArray[index].Luma = ColorToLuma(UndenoisedTexture[DTid.xy]);
			lumaAndCoordArray[index].Coord = Coord;
			index++;
		}
	}

	for (uint i = 0; i < cArraySize - 1; i++)
	{
		uint lowestLumaIndex = i;
		for (uint j = i + 1; j < cArraySize; j++)
		{
			if (lumaAndCoordArray[j].Luma < lumaAndCoordArray[lowestLumaIndex].Luma)
			{
				lowestLumaIndex = j;
			}
		}
		LumaAndCoord temp = lumaAndCoordArray[i];
		lumaAndCoordArray[i] = lumaAndCoordArray[lowestLumaIndex];
		lumaAndCoordArray[lowestLumaIndex] = temp;
	}
	return lumaAndCoordArray[5].Coord;
}

#define USE_MEDIAN_FILTER 0
#define ComputeRS \
    "RootConstants(num32BitConstants=7, b0),\
    DescriptorTable(SRV(t0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(UAV(u0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t1, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t2, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t3, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t4, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL)"



[RootSignature(ComputeRS)]
[numthreads(DENOISER_THREAD_GROUP_WIDTH, DENOISER_THREAD_GROUP_HEIGHT, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	uint2 medianCoord = DTid.xy;
#if USE_MEDIAN_FILTER
	if (Constants.OffsetMultiplier <= 1.0f)
	{
		medianCoord = GetMedianCoord(DTid);
	}
#endif

	float3 normal = GetNormal(medianCoord);
	float4 intersectedPositionData = AOVIntersectPosition[medianCoord];
	float3 intersectedPosition = intersectedPositionData.xyz;
	float distanceToNeighborPixel = intersectedPositionData.w;
	float luma = ColorToLuma(UndenoisedTexture[medianCoord]);

	float luminanceVariance = InputTexture[medianCoord].w;

	float weightedSum = 0.0f;
	float accumulatedVariance = 0.0;
	float3 accumulatedColor = float3(0, 0, 0);

	if(ValidNormal(normal))
	{
		for (int xOffset = -KERNEL_WIDTH / 2; xOffset <= KERNEL_WIDTH / 2; xOffset++)
		{
			for (int yOffset = -KERNEL_WIDTH / 2; yOffset <= KERNEL_WIDTH / 2; yOffset++)
			{
				int2 offsetCoord = int2(xOffset, yOffset) * Constants.OffsetMultiplier;
				int2 coord = medianCoord + offsetCoord;

				if (coord.x < 0 || coord.y < 0 || coord.x >= Constants.Resolution.x || coord.y >= Constants.Resolution.y)
				{
					continue;
				}

				float weight = CalculateWeight(luma, luminanceVariance, normal, intersectedPosition, distanceToNeighborPixel, coord, offsetCoord);
				float4 NeighborData = InputTexture[coord];
				float NeighborVariance = NeighborData.a;
				float3 NeighborColor = NeighborData.rgb;

				accumulatedColor += weight * NeighborColor;
				accumulatedVariance += weight * weight * NeighborVariance;
				weightedSum += weight;
			}
		}
	}
	else
	{
		accumulatedVariance = InputTexture[medianCoord].w;
		accumulatedColor = InputTexture[medianCoord].xyz;
		weightedSum = 1.0f;
	}
	
	
	OutputTexture[DTid.xy] = float4(accumulatedColor / weightedSum, accumulatedVariance / (weightedSum * weightedSum));
}