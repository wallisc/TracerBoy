#define HLSL
#include "CalculateVarianceSharedShaderStructs.h"
#include "Tonemap.h"

Texture2D SummedLumaSquared: register(t0);
Texture2D PathTracingOutput: register(t1);

RWTexture2D<float2> LuminanceVariance : register(u0);

cbuffer CalculateVarianceCB
{
	CalculateVarianceConstants Constants;
}

[numthreads(CALCULATE_VARIANCE_THREAD_GROUP_WIDTH, CALCULATE_VARIANCE_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;



	float FrameCount = PathTracingOutput[float2(0, Constants.Resolution.y - 1)].x;
	
	float neighborVariance = 0.0;
	const uint cFramesToRelyOnNeighbor = 16;
	if (Constants.GlobalFrameCount <= cFramesToRelyOnNeighbor)
	{
		const int kernelWidth = 5;
		uint count = 0;
		float summedLuma = 0.0;
		for (int x = -kernelWidth / 2; x < kernelWidth / 2; x++)
		{
			for (int y = -kernelWidth / 2; y < kernelWidth / 2; y++)
			{
				int2 sampleCoord = int2(DTid.xy)+int2(x, y);
				if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
					sampleCoord.x >= Constants.Resolution.x || sampleCoord.y >= Constants.Resolution.y) continue;
				summedLuma += ColorToLuma(PathTracingOutput[sampleCoord] / Constants.GlobalFrameCount);
				count++;
			}
		}
		float  lumaMean = summedLuma / count;

		float summedVariance = 0.0;
		for (int x = -kernelWidth / 2; x < kernelWidth / 2; x++)
		{
			for (int y = -kernelWidth / 2; y < kernelWidth / 2; y++)
			{
				int2 sampleCoord = int2(DTid.xy)+int2(x, y);
				if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
					sampleCoord.x >= Constants.Resolution.x || sampleCoord.y >= Constants.Resolution.y) continue;
				summedVariance += (lumaMean - ColorToLuma(PathTracingOutput[sampleCoord] / Constants.GlobalFrameCount)) * (lumaMean - ColorToLuma(PathTracingOutput[sampleCoord] / Constants.GlobalFrameCount));
			}
		}

		neighborVariance = summedVariance / (count - 1);
	}	
	

	float meanLuma = ColorToLuma(PathTracingOutput[DTid.xy] / FrameCount);
	float lumaSquared = SummedLumaSquared[DTid.xy] / FrameCount;
	float temporalVariance = abs(lumaSquared - meanLuma * meanLuma);

	LuminanceVariance[DTid.xy] = lerp(neighborVariance, temporalVariance, 
		min(float(Constants.GlobalFrameCount - 1) / float(cFramesToRelyOnNeighbor), 1.0f));

}