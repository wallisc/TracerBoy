#define IS_COMPUTE_SHADER 1

#include "RayGenCommon.h"
#include "ComputeShaderUtil.h"

#define ComputeRS \
    "RootConstants(num32BitConstants=43, b0)," \
	"CBV(b1)," \
    "DescriptorTable(UAV(u2, numDescriptors=6), visibility=SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(UAV(u0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(UAV(u1, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL)," \
    "SRV(t1)," \
    "DescriptorTable(SRV(t20, numDescriptors=3), visibility=SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(SRV(t14, numDescriptors=2), visibility=SHADER_VISIBILITY_ALL)," \
    "DescriptorTable(SRV(t0, space = 1, numDescriptors=unbounded)," \
		"SRV(t0, space = 2, numDescriptors=unbounded, offset=0)," \
		"SRV(t0, space = 3, numDescriptors=unbounded, offset=0), visibility=SHADER_VISIBILITY_ALL)," \
    "SRV(t11)," \
    "SRV(t23)," \
    "UAV(u10)," \
    "UAV(u11)," \
    "DescriptorTable(SRV(t0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL)," \
	"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)," \
	"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP)," \
	"StaticSampler(s2, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)"

[RootSignature(ComputeRS)]
[numthreads(RAYTRACE_THREAD_GROUP_WIDTH, RAYTRACE_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint Gindex : SV_GroupIndex)
{
	OutputTexture.GetDimensions(Resolution.x, Resolution.y);

	uint2 GroupDimensions = uint2(
		(Resolution.x - 1) / RAYTRACE_THREAD_GROUP_WIDTH + 1,
		(Resolution.y - 1) / RAYTRACE_THREAD_GROUP_HEIGHT + 1);
	uint TotalDispatchCount = GroupDimensions.x * GroupDimensions.y;

	uint2 ThreadGroupDimensions = uint2 (RAYTRACE_THREAD_GROUP_WIDTH, RAYTRACE_THREAD_GROUP_HEIGHT);
	DTid.xy = ThreadGroupTilingX(
		Resolution / ThreadGroupDimensions,
		ThreadGroupDimensions,
		16,
		GTid.xy,
		Gid.xy);

	DispatchIndex = DTid.xy;

	if (any(DTid.xy >= Resolution)) return;

	ClearAOVs();
	seed = hash13(float3(GetDispatchIndex().x, GetDispatchIndex().y, perFrameConstants.GlobalFrameCount));

#if USE_ADAPTIVE_RAY_DISPATCHING
	if (!perFrameConstants.IsRealTime)
	{
		bool bSkipRay = ShouldSkipRay();
		OutputLivePixels(bSkipRay);

		uint activeRayCount = WaveActiveCountBits(!bSkipRay);
		if (activeRayCount > 0 && WaveIsFirstLane())
		{
			uint unused;
			StatsBuffer.InterlockedAdd(0, 1, unused);
			StatsBuffer.InterlockedAdd(4, activeRayCount, unused);
		}

		if (bSkipRay) return;


	}
#endif

	RayTraceCommon();
}