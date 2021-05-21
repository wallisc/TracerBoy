#define IS_COMPUTE_SHADER 1

#include "RayGenCommon.h"
#include "ComputeShaderUtil.h"

[numthreads(RAYTRACE_THREAD_GROUP_WIDTH, RAYTRACE_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint Gindex : SV_GroupIndex)
{
	OutputTexture.GetDimensions(Resolution.x, Resolution.y);

	if (perFrameConstants.UseExecuteIndirect)
	{
		uint TotalRayCount = IndirectArgsBuffer.Load(3 * 4) * RAYTRACE_THREAD_GROUP_HEIGHT * RAYTRACE_THREAD_GROUP_WIDTH;
		uint FlatIndex = Gid.x * RAYTRACE_THREAD_GROUP_HEIGHT * RAYTRACE_THREAD_GROUP_WIDTH + GTid.y * RAYTRACE_THREAD_GROUP_WIDTH + GTid.x;
		FlatIndex = FlatIndex% TotalRayCount;
		
		DispatchIndex = RayIndexBuffer[FlatIndex].xy;
		if (DispatchIndex.x == uint(-1)) return;

		// Hack for visualization
		OutputLiveWaves(Gid.x);
	}
	else
	{
		uint2 ThreadGroupDimensions = uint2 (RAYTRACE_THREAD_GROUP_WIDTH, RAYTRACE_THREAD_GROUP_HEIGHT);
		DTid.xy = ThreadGroupTilingX(
			Resolution / ThreadGroupDimensions,
			ThreadGroupDimensions,
			16,
			GTid.xy,
			Gid.xy);

		DispatchIndex = DTid;

		if (any(DTid.xy >= Resolution)) return;
	}

	ClearAOVs();
	seed = hash13(float3(GetDispatchIndex().x, GetDispatchIndex().y, perFrameConstants.GlobalFrameCount));

#if USE_ADAPTIVE_RAY_DISPATCHING
	if (!perFrameConstants.UseExecuteIndirect)
	{
		bool bSkipRay = ShouldSkipRay();
		OutputLivePixels(bSkipRay);
		if (bSkipRay) return;
	}
#endif

	RayTraceCommon();
}