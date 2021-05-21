#include "SharedWaveCompactionStructs.h"

RWByteAddressBuffer IndirectArg : register(u3);

cbuffer RootConstants
{
	WaveCompactionConstants Constants;
}

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint WaveCount = IndirectArg.Load(0);
	if (WaveCount > 0)
	{
		IndirectArg.Store(0, max(Constants.MinWaveCount, WaveCount));
	}
}