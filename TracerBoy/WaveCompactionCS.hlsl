#define HLSL
#include "SharedShaderStructs.h"
#include "SharedWaveCompactionStructs.h"
#include "VarianceUtil.h"
#include "ComputeShaderUtil.h"

RWTexture2D<float4> OutputTexture : register(u0);
RWTexture2D<float4> JitteredOutputTexture : register(u1);

RWStructuredBuffer<uint2> RayIndexBuffer : register(u2);
RWByteAddressBuffer IndirectArg : register(u3);

cbuffer RootConstants
{
	WaveCompactionConstants Constants;
}

groupshared uint ActiveRayCount;
groupshared uint IndirectArgOutputIndex;
groupshared uint2 GroupActiveRays[TILE_WIDTH * TILE_HEIGHT];

[numthreads(RAYTRACE_THREAD_GROUP_WIDTH, RAYTRACE_THREAD_GROUP_HEIGHT, 1)]
void main(uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID )
{
	uint2 Resolution;
	OutputTexture.GetDimensions(Resolution.x, Resolution.y);

	uint2 TileDimensions = uint2(TILE_WIDTH, TILE_HEIGHT);
	uint2 WaveDimensions = uint2(RAYTRACE_THREAD_GROUP_WIDTH, RAYTRACE_THREAD_GROUP_HEIGHT);
	uint WaveSize = WaveDimensions.x * WaveDimensions.y;

	ThreadGroupTilingX(
		Resolution / TileDimensions,
		WaveDimensions,
		8,
		GTid.xy,
		Gid.xy);

	uint FlatGroupThreadID = GTid.x + GTid.y * RAYTRACE_THREAD_GROUP_WIDTH;
	if (all(GTid == 0))
	{
		ActiveRayCount = 0;
	}
	uint WavesPerTileRow = TILE_WIDTH / RAYTRACE_THREAD_GROUP_WIDTH;
	uint WavesPerTileColumn = TILE_HEIGHT / RAYTRACE_THREAD_GROUP_HEIGHT;

	uint WavesPerTile = TILE_WIDTH * TILE_HEIGHT / WaveSize;
	for (uint i = 0; i < WavesPerTile; i++)
	{
		GroupActiveRays[FlatGroupThreadID + i * WaveSize] = uint2(-1, -1);
	}
	GroupMemoryBarrierWithGroupSync();

	uint2 TopLeftGroupTileIndex = Gid * TileDimensions;
	for (uint y = 0; y < WavesPerTileColumn; y++)
	{
		for (uint x = 0; x < WavesPerTileRow; x++)
		{
			uint2 TopLeftTileIndex = TopLeftGroupTileIndex + uint2(x, y) * WaveDimensions;
			uint2 TileIndex = TopLeftTileIndex + GTid;

			if (all(TileIndex < Resolution) && !ShouldSkipRay(OutputTexture, JitteredOutputTexture, TileIndex, Constants.MinConvergence, Constants.GlobalFrameCount))
			{
				uint IndexToStore;
				InterlockedAdd(ActiveRayCount, 1, IndexToStore);
				GroupActiveRays[IndexToStore] = TileIndex;
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();
	if (ActiveRayCount == 0) return;

	uint NumWaves = (ActiveRayCount - 1) / WaveSize + 1;
	if (all(GTid == 0))
	{
		IndirectArg.InterlockedAdd(0, NumWaves, IndirectArgOutputIndex);
		IndirectArg.InterlockedAdd(12, NumWaves, IndirectArgOutputIndex);
	}

	GroupMemoryBarrierWithGroupSync();

	uint OutputIndex = IndirectArgOutputIndex * WaveSize;
	for (uint i = 0; i < NumWaves; i++)
	{
		uint RayIndex = i * WaveSize + FlatGroupThreadID;
		RayIndexBuffer[OutputIndex + RayIndex] = GroupActiveRays[RayIndex];
	}
}