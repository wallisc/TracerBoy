#define HLSL
#include "DenoiserSharedShaderStructs.h"

Texture2D InputTexture : register(t0);
Texture2D AOVNormals : register(t1);
RWTexture2D<float4> OutputTexture;

cbuffer DenoiserCB
{
	DenoiserConstants Constants;
}

#define KERNEL_WIDTH 5

float CalculateWeight(float3 centerNormal, int2 coord, int2 offset)
{
	float3 normal = AOVNormals[coord];
	float normalWeight = max(0.0f, dot(centerNormal, normal));

	float weights[(KERNEL_WIDTH / 2) + 1] = { 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };
	return normalWeight * weights[abs(offset.x)] * weights[abs(offset.y)];
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
	float3 normal = AOVNormals[DTid.xy].xyz;

	float weightedSum = 0.0f;
	float3 accumulatedColor = float3(0, 0, 0);

	if(ValidNormal(normal) && DTid.x < Constants.Resolution.x / 2 )
	{
		for (int xOffset = -KERNEL_WIDTH / 2; xOffset <= KERNEL_WIDTH / 2; xOffset++)
		{
			for (int yOffset = -KERNEL_WIDTH / 2; yOffset <= KERNEL_WIDTH / 2; yOffset++)
			{
				int2 offsetCoord = int2(xOffset, yOffset);
				int2 coord = DTid.xy + offsetCoord;

				if (coord.x < 0 || coord.y < 0 || coord.x > Constants.Resolution.x || coord.y > Constants.Resolution.y)
				{
					continue;
				}

				float weight = CalculateWeight(normal, coord, offsetCoord);

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