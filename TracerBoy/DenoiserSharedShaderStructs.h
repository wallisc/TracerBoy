#include "SharedShaderStructs.h"

#define DENOISER_THREAD_GROUP_WIDTH 8
#define DENOISER_THREAD_GROUP_HEIGHT 8

struct DenoiserConstants
{
	uint2 Resolution;
	uint OffsetMultiplier;
	float NormalWeightingExponential;
	float IntersectionPositionWeightingMultiplier;
	float LumaWeightingMultiplier;
	uint GlobalFrameCount;
};

