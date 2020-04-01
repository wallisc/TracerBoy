#include "SharedShaderStructs.h"

#define DENOISER_THREAD_GROUP_WIDTH 1
#define DENOISER_THREAD_GROUP_HEIGHT 1

struct DenoiserConstants
{
	uint2 Resolution;
	uint OffsetMultiplier;
	float NormalWeightingExponential;
	float IntersectionPositionWeightingMultiplier;
	float LumaWeightingMultiplier;
};

