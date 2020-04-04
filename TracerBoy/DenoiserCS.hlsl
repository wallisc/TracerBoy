#define HLSL
#include "DenoiserSharedShaderStructs.h"
#include "Tonemap.h"

Texture2D InputTexture : register(t0);
Texture2D AOVNormals : register(t1);
Texture2D AOVIntersectPosition : register(t2);
StructuredBuffer<SDRHistogram> AOVSDRHistogram: register(t3);
Texture2D UndenoisedTexture : register(t4);

RWTexture2D<float4> OutputTexture;

cbuffer DenoiserCB
{
	DenoiserConstants Constants;
}

#define KERNEL_WIDTH 5

float CalculateVariance(SDRHistogram histogram)
{
	float bucketSize = 1.0f / float(NUM_HISTOGRAM_BUCKETS);

	uint totalCounts = 0;
	float lumaSum = 0.0f;

	[unroll]
	for (uint i = 0; i < NUM_HISTOGRAM_BUCKETS; i++)
	{
		float lumaValue = bucketSize * i + bucketSize * 0.5f;
		uint count = histogram.Count[i];
		lumaSum += count * lumaValue;
		totalCounts += count;
	}

	float lumaAverage = lumaSum / float(totalCounts);
	float lumaVarianceSum = 0.0f;

	[unroll]
	for (uint i = 0; i < NUM_HISTOGRAM_BUCKETS; i++)
	{
		float lumaValue = bucketSize * i + bucketSize * 0.5f;
		uint count = histogram.Count[i];
		lumaVarianceSum += count * (lumaAverage - lumaValue) * (lumaAverage - lumaValue);
	}

	return lumaVarianceSum / float(totalCounts);
}

float3 GetNormal(int2 coord)
{
	float4 normalData = AOVNormals[coord];
	if (normalData.w == 0.0f) return float3(0.0f, 0.0f, 0.0f);

	return normalize(normalData.xyz / normalData.w);
}

float CalculateWeight(
	float centerLuma,
	float3 centerNormal, 
	float3 centerIntersectPosition, 
	float distanceToNeighborPixel, 
	int2 coord, 
	int2 offset,
	int FrameCount)
{
#if 0
	SDRHistogram histogram = AOVSDRHistogram[coord.x + coord.y * Constants.Resolution.x];
	float lumaVariance = CalculateVariance(histogram);
	float luma = HDRToLuma(UndenoisedTexture[coord] / FrameCount);
	float lumaWeight = exp(-abs(luma - centerLuma) / (Constants.LumaWeightingMultiplier * sqrt(lumaVariance) + EPSILON));
#endif
	float lumaWeight = 1.0f;

	float3 normal = GetNormal(coord);
	float normalWeightExponential = 128.0f;
	float normalWeight = pow(max(0.0f, dot(centerNormal, normal)), Constants.NormalWeightingExponential);

	float positionWeightMultiplier = 1.0f;
	float3 intersectedPosition = AOVIntersectPosition[coord].xyz;
	float distance = length(intersectedPosition - centerIntersectPosition);
	float positionWeight = exp(-distance / (Constants.IntersectionPositionWeightingMultiplier * abs(dot(offset, float2(distanceToNeighborPixel, distanceToNeighborPixel))) + EPSILON));

	float weights[(KERNEL_WIDTH / 2) + 1] = { 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };
	return lumaWeight * positionWeight* normalWeight* weights[abs(offset.x / int(Constants.OffsetMultiplier))] * weights[abs(offset.y / int(Constants.OffsetMultiplier))];
}

bool ValidNormal(float3 normal)
{
	return normal.x != 0.0f || normal.y != 0.0f || normal.z != 0.0f;
}

[numthreads(DENOISER_THREAD_GROUP_WIDTH, DENOISER_THREAD_GROUP_HEIGHT, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	if (DTid.x == 0 && DTid.y == Constants.Resolution.y - 1)
	{
		OutputTexture[DTid.xy] = float4(1, 1, 1, 1);
	}

	float FrameCount = InputTexture[float2(0, Constants.Resolution.y - 1)].x;
	float UndenoisedFrameCount = UndenoisedTexture[float2(0, Constants.Resolution.y - 1)].x;
	float3 normal = GetNormal(DTid.xy);
	float4 intersectedPositionData = AOVIntersectPosition[DTid.xy];
	float3 intersectedPosition = intersectedPositionData.xyz;
	float distanceToNeighborPixel = intersectedPositionData.w;
	float luma = HDRToLuma(UndenoisedTexture[DTid.xy] / UndenoisedFrameCount);

	float weightedSum = 0.0f;
	float3 accumulatedColor = float3(0, 0, 0);

	if(ValidNormal(normal) && DTid.x < Constants.Resolution.x / 2 )
	{
		for (int xOffset = -KERNEL_WIDTH / 2; xOffset <= KERNEL_WIDTH / 2; xOffset++)
		{
			for (int yOffset = -KERNEL_WIDTH / 2; yOffset <= KERNEL_WIDTH / 2; yOffset++)
			{
				int2 offsetCoord = int2(xOffset, yOffset) * Constants.OffsetMultiplier;
				int2 coord = DTid.xy + offsetCoord;

				if (coord.x < 0 || coord.y < 0 || coord.x > Constants.Resolution.x || coord.y > Constants.Resolution.y)
				{
					continue;
				}

				float weight = CalculateWeight(luma, normal, intersectedPosition, distanceToNeighborPixel, coord, offsetCoord, UndenoisedFrameCount);

				accumulatedColor += weight * InputTexture[coord].xyz / FrameCount;
				weightedSum += weight;
			}
		}
	}
	else
	{
		accumulatedColor = InputTexture[DTid.xy].xyz / FrameCount;
		weightedSum = 1.0f;
	}
	

	OutputTexture[DTid.xy] = float4(accumulatedColor / weightedSum, 1);
}