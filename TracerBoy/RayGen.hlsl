#include "RayGenCommon.h"

[shader("raygeneration")]
void RayGen()
{
	ClearAOVs();

	seed = hash13(float3(DispatchRaysIndex().x, DispatchRaysIndex().y, perFrameConstants.GlobalFrameCount));
	Resolution = DispatchRaysDimensions();
	DispatchIndex = DispatchRaysIndex().xy;

#if USE_ADAPTIVE_RAY_DISPATCHING
	bool bSkipRay = ShouldRayBeSkipped();
	OutputLivePixels(bSkipRay);
	if (bSkipRay) return;
#endif

	RayTraceCommon();
}
