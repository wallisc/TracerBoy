#include "CalculateAveragedLuminanceSharedShaderStructs.h"

ByteAddressBuffer LuminanceHistogram : register(t0);
RWByteAddressBuffer AveragedLuminance : register(u0);

cbuffer CalculateAveragedCB
{
	CalculateAveragedLuminanceConstants Constants;
}

groupshared uint AveragedHistogramCount;

[numthreads(CALCULATE_AVERAGED_LUMINANCE_THREAD_GROUP_WIDTH, CALCULATE_AVERAGED_LUMINANCE_THREAD_GROUP_HEIGHT, 1)]
void main(uint Gid : SV_GroupIndex )
{
	bool bIsFirstThread = (Gid== 0);
	if(bIsFirstThread)
	{
		AveragedHistogramCount = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	uint BinIndex = Gid;
	uint BinCount = LuminanceHistogram.Load(BinIndex * BYTE_ADDRESS_BUFFER_STRIDE);

	InterlockedAdd(AveragedHistogramCount, BinCount * BinIndex);

	GroupMemoryBarrierWithGroupSync();

	if(bIsFirstThread)
	{
		float averagedLogLuminance = ((AveragedHistogramCount / Constants.PixelCount) - 1.0f) / 254.0f;
		AveragedLuminance.Store(0, asuint(exp2(averagedLogLuminance * Constants.LogLuminanceRange + Constants.MinLogLuminance)));
	}
}